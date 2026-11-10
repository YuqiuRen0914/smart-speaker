#pragma once
namespace lunaredu::lunaros { struct ProvisioningPolicy; }
#include <cstddef>
#include <cstdint>
#include <string>
#include "kernel/product_definition.h"
#include "runtime/preset.h"
#include "app/lifecycle/runtime_application_entry.h"
#include "device_api/projection_contribution.h"
#include "lunaros_network_policy.h"
#include "lunaredu_local_web_config.h"
#include "lunaros_resource_manifest.h"
#include "runtime/module_maintenance.h"
#include "app/execution/application_device.h"

namespace lunaredu::lunaros {
// Non-owning immutable array. Declarations and callback contexts live for the
// whole boot, just like service descriptors. No initializer_list temporaries.
template <class T> struct DeviceItems {
  const T *data = nullptr;
  size_t size = 0;
  constexpr DeviceItems() = default;
  constexpr DeviceItems(const T *items, size_t count) : data(items), size(count) {}
  template <size_t N> constexpr DeviceItems(const T (&items)[N]) : data(items), size(N) {}
};

struct BooleanControl {
  const char *id;
  const char *label;
  bool (*read)(bool &value, std::string &error);
  bool (*write)(bool value, std::string &error);
  // Optional immutable product wiring; -1 leaves the physical binding unknown.
  int diagnostic_gpio = -1;
  bool diagnostic_active_low_known = false;
  bool diagnostic_active_low = false;
  const char *diagnostic_resource = nullptr;
};

struct DeviceOptions {
  const char *name = "LunarOS";
  const RuntimePreset *preset = nullptr; // nullptr selects the existing OS preset.
  bool (*initialize)(std::string &error) = nullptr;
  DeviceItems<BooleanControl> controls{};
  NetworkPolicy network{};
  // Borrowed until synchronous start returns; nullptr selects service defaults.
  const lunaredu::lunaros::ProvisioningPolicy *provisioning = nullptr;
  const char *timezone = "CST-8";
  DeviceWebConfig web{};
  DeviceItems<ServiceNode> services{};
  DeviceItems<ProductDeclarationNode> declarations{};
  DeviceItems<ManagedAssetDefinition> assets{};
  uint32_t boot_healthy_period_ms = 3000;
  std::span<const RuntimeHealthProbe> health{};
  // Execution-provider foreground gate, supplied by the product.
  void *application_foreground_context = nullptr;
  bool (*application_foreground_transition)(void *, bool, std::string &) = nullptr;
  // Default foreground assembly is independent of the selected VM backends.
  // Pure service devices can opt out; a product-owned controller stays borrowed.
  app::RuntimeApplicationSelection applications{};
  // Borrowed, explicit protocol declarations. Selecting an App creates no
  // protocol surface; products choose the two contributions independently.
  DeviceProjectionSelection projections{};
  bool enable_applications = true;
  // Synchronous startup-only observation, including failures before rollback.
  void (*startup_observer)(void *,const char *,const char *,const char *) = nullptr;
  void *startup_observer_context = nullptr;
  DeviceItems<ModuleMaintenance> module_maintenance{};
  apps::ApplicationDevicePorts application_devices{};
};

} // namespace lunaredu::lunaros
