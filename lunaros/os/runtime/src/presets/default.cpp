#include "device_api/adapters/application_projection.h"
#include "device_api/adapters/identity_projection.h"
#include "lunaros_applications_service_id.h"
#include "composition/runtime_projections.h"
#include "lunaros_ota_projection.h"
#include "lunaros_network_projection.h"
#include "device_api/core_projections.h"
#ifdef LUNAROS_PROVISIONING_COMPONENT
#include "lunaros_provisioning.h"
#endif
#include "presets/default.h"
#include "runtime/product_composition.h"
#include "runtime/application_host.h"
#include <array>
#include <sdkconfig.h>
#include "startup/product_runtime_internal.h"
#include <algorithm>
#include <cstring>
#include <vector>
#include "app/lifecycle/app_controller.h"
#include "kernel/runtime.h"
#include "runtime/device_context.h"
#include "lunaros_network_lifecycle.h"
#include "lunaros_network.h"
#include "lunaros_network_http_client.h"
#include "device_api/device_api.h"
#include "lunaros_usb.h"
#include "lunaros_local_web_lifecycle.h"
#include "device_api/device_projection.h"
#include "lunaros_resource.h"
#include "lunaros_download_lifecycle.h"
#include "device_api/projection_composition.h"
#include "lunaros_storage.h"
#include "lunaros_applications.h"
#include "lunaredu_ota.h"
#include "runtime/system/system.h"
#include "lunaros_factory_lifecycle.h"
#include "lunaros_factory_projection.h"
#ifdef LUNAROS_DIAGNOSTICS_COMPONENT
#include "lunaros_diagnostics.h"
#endif


#include "runtime/preset.h"
#include "presets/backends.h"
#include "presets/execution.h"
namespace lunaredu::lunaros {
RuntimeHealthObservation observe_default_boot_health(DeviceContext &);
namespace {
struct RankedProjection {
  const StandardProjectionContribution *contribution = nullptr;
  unsigned order = 0;
};
struct RuntimeServiceEntry {
  ServiceNode (*create)(DeviceContext &, DeviceProjectionRegistry &) = nullptr;
  std::array<RankedProjection, 4> projections{};
};
template<class... Projections>
constexpr RuntimeServiceEntry service_entry(ServiceNode (*create)(DeviceContext &, DeviceProjectionRegistry &), Projections... projections) {
  static_assert(sizeof...(Projections) <= 4);
  return {create, {projections...}};
}
// The OS preset selects each service and its projection contributions together.
// Projection rank preserves the established declaration order independently of
// service dependency order; these descriptions never call a service factory.
const StandardProjectionContribution kFactoryProjection{"factory", declare_local_factory_projection};
#ifdef LUNAROS_PROVISIONING_COMPONENT
const StandardProjectionContribution kProvisioningProjection{"provisioning",declare_provisioning_projection};
#endif
constexpr RuntimeServiceEntry kDefaultServices[] = {
#ifdef LUNAROS_PROVISIONING_COMPONENT
  service_entry([](DeviceContext &device, DeviceProjectionRegistry &){return provisioning_service_node(device);},RankedProjection{&kProvisioningProjection,10}),
#endif
  service_entry([](DeviceContext &, DeviceProjectionRegistry &) { return factory_service_node(); },
      RankedProjection{&kFactoryProjection, 9}),
  service_entry(nullptr, RankedProjection{&standard_projection::activities, 0},
      RankedProjection{&standard_projection::services, 1}, RankedProjection{&standard_projection::tasks, 2},
      RankedProjection{&standard_projection::identity, 3}),
  service_entry([](DeviceContext &, DeviceProjectionRegistry &) { return system_service_node(); },
      RankedProjection{&standard_projection::system, 4}, RankedProjection{&standard_projection::debug, 8}),
  service_entry([](DeviceContext &, DeviceProjectionRegistry &p) { return storage_service_node(p.context().capabilities); },
      RankedProjection{&standard_projection::storage, 5}),
  service_entry([](DeviceContext &, DeviceProjectionRegistry &p) { return update_service_node(p.context().capabilities); },
      RankedProjection{&standard_projection::update, 7}),
  service_entry([](DeviceContext &device, DeviceProjectionRegistry &) { return network_service_node(device); },
      RankedProjection{&standard_projection::network, 6}),
  service_entry([](DeviceContext &, DeviceProjectionRegistry &) {
      static DownloadServicePorts download{network_open_download_stream}; return download_service_node(download);
  }),
  service_entry([](DeviceContext &, DeviceProjectionRegistry &) { return resource_service_node(); }),
  service_entry([](DeviceContext &, DeviceProjectionRegistry &p) { return device_api_service_node(p); }),
  service_entry([](DeviceContext &, DeviceProjectionRegistry &p) { return usb_service_node(p); }),
#ifdef LUNAROS_DIAGNOSTICS_COMPONENT
  service_entry([](DeviceContext &, DeviceProjectionRegistry &) { return diagnostics_service_node(); }),
#endif
};
// The preset and its order are immutable. Build the borrowed projection range
// at compile time, rather than allocating/sorting it on first installation.
constexpr auto kDefaultProjections = [] {
  constexpr size_t count = [] {
    size_t size = 0;
    for (const auto &entry : kDefaultServices)
      for (const auto &projection : entry.projections)
        if (projection.contribution) ++size;
    return size;
  }();
  std::array<RankedProjection, count> ranked{};
  size_t size = 0;
  for (const auto &entry : kDefaultServices) {
    for (const auto &projection : entry.projections) {
      if (!projection.contribution) continue;
      ranked[size++] = projection;
    }
  }
  for (size_t pass = 0; pass < count; ++pass)
    for (size_t i = 1; i < count - pass; ++i)
      if (ranked[i - 1].order > ranked[i].order) {
        const auto previous = ranked[i - 1];
        ranked[i - 1] = ranked[i];
        ranked[i] = previous;
      }
  std::array<const StandardProjectionContribution *, count> result{};
  for (size_t i = 0; i < count; ++i) result[i] = ranked[i].contribution;
  return result;
}();
}
std::vector<ServiceNode> default_service_nodes(DeviceContext &device, DeviceProjectionRegistry &projections, apps::ApplicationsService &applications,
    const ServiceDependency *engines, size_t engine_count) {
  std::vector<ServiceNode> nodes;
  nodes.reserve(std::size(kDefaultServices) + 1);
  nodes.push_back(applications.node(projections.runtime(), engines, engine_count));
  for (const auto &entry : kDefaultServices) if (entry.create) nodes.push_back(entry.create(device, projections));
  return nodes;
}
bool declare_default_projections(void *context, std::string &error) {
  if (!context) { error = "projection_context_missing"; return false; }
  auto &registry = *static_cast<DeviceProjectionRegistry *>(context);
  return install_standard_device_projections(kDefaultProjections.data(), kDefaultProjections.size(), registry, error);
}
bool register_default_foreground(app::AppController &applications, const char *name, std::string &error) {
  app::ForegroundDefinition idle;
  idle.id = "system.idle";
  idle.manifest.title = name;
  idle.manifest.kind = app::ForegroundKind::SystemSurface;
  return applications.register_foreground(idle, error);
}
const RuntimePreset &default_runtime_preset() {
  static constexpr auto factories = [] {
    constexpr size_t count = [] { size_t n=0; for (const auto &entry:kDefaultServices) if(entry.create) ++n; return n; }();
    std::array<RuntimeServiceFactory, count> result{};
    size_t n=0; for (const auto &entry:kDefaultServices) if(entry.create) result[n++]=entry.create;
    return result;
  }();
  static constexpr RuntimePreset preset{"lunaros.default", factories.data(), factories.size(),
      kDefaultProjections.data(), kDefaultProjections.size(), nullptr, {}, observe_default_boot_health, true,
      default_execution_contributions()};
  return preset;
}
} // namespace lunaredu::lunaros
