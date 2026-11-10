#pragma once
#include "device_api/projection_contribution.h"
#include "device_api/device_api_context.h"
#include "app/execution/application_context.h"
#include "app/lifecycle/app_controller.h"
#include "app/lifecycle/app_controller_view.h"
namespace lunaredu::lunaros {
class ApplicationProtocolAccess;
// Narrow command port; the adapter resolves the typed owner and Application
// performs the existing executor-qualified dispatch.
bool device_api_switch_application(DeviceApiContext &, const std::string &,
    app::AppControllerStatusSnapshot &, std::string &error);
app::CapabilityRegistry *device_api_application_capabilities(const DeviceApiContext &);
void *device_api_application_foreground(const DeviceApiContext &, const char *id);
bool device_api_has_applications(const DeviceApiContext &);
const apps::ApplicationMaintenancePorts &device_api_application_maintenance(const DeviceApiContext &);
// Application owns the typed view. Consumers receive only the particular
// capability catalog, foreground owner or maintenance port they need.
class ApplicationProtocolBinding {
public:
  ApplicationProtocolBinding(DeviceApiContext &api, ApplicationContext &application)
      : application_(application), binding_(api, &key_,
          &static_cast<ExecutionContext &>(api) == &static_cast<ExecutionContext &>(application)
              ? this : nullptr) {}
  ApplicationProtocolBinding(const ApplicationProtocolBinding &) = delete;
  ApplicationProtocolBinding &operator=(const ApplicationProtocolBinding &) = delete;
  bool owns_context() const { return binding_.owns_context(); }
  static const ApplicationProtocolBinding *bound(const DeviceApiContext &api) {
    return static_cast<const ApplicationProtocolBinding *>(api.binding(&key_));
  }
private:
  friend class ApplicationProtocolAccess;
  friend app::CapabilityRegistry *device_api_application_capabilities(const DeviceApiContext &);
  friend void *device_api_application_foreground(const DeviceApiContext &, const char *);
  friend bool device_api_has_applications(const DeviceApiContext &);
  friend const apps::ApplicationMaintenancePorts &device_api_application_maintenance(const DeviceApiContext &);
  inline static const char key_ = 0;
  ApplicationContext &application_;
  DeviceApiBinding binding_;
};
inline app::CapabilityRegistry *device_api_application_capabilities(const DeviceApiContext &device) {
  const auto *binding = ApplicationProtocolBinding::bound(device);
  auto *controller = binding ? binding->application_.application() : nullptr;
  return controller ? &controller->capabilities() : nullptr;
}
inline void *device_api_application_foreground(const DeviceApiContext &device, const char *id) {
  const auto *binding = ApplicationProtocolBinding::bound(device);
  const auto *controller = binding ? binding->application_.application() : nullptr;
  return controller ? controller->foreground_context(id) : nullptr;
}
inline bool device_api_has_applications(const DeviceApiContext &device) {
  const auto *binding = ApplicationProtocolBinding::bound(device);
  const auto *controller = binding ? binding->application_.application() : nullptr;
  return controller && app::AppControllerView{controller}.has_applications();
}
inline const apps::ApplicationMaintenancePorts &device_api_application_maintenance(const DeviceApiContext &device) {
  static const apps::ApplicationMaintenancePorts unavailable{};
  const auto *binding = ApplicationProtocolBinding::bound(device);
  return binding ? apps::application_maintenance(binding->application_) : unavailable;
}
namespace standard_projection { extern const StandardProjectionContribution applications; }
}
