#include "model/device_model.h"
#include "lunaros_network_lifecycle.h"
#include "sdkconfig.h"

#include <esp_log.h>

namespace {

constexpr const char *kNetworkPresenceTags[] = {
    "wifi_sta",
#if CONFIG_LUNAROS_NETWORK_LOCAL_ACCESS
    "wifi_ap",
#endif
#if CONFIG_LUNAROS_NETWORK_LOCAL_GATT && CONFIG_BT_NIMBLE_ENABLED
    "ble_local_gatt",
#endif
};

const lunaredu::lunaros::DeviceCapabilityDescriptor kNetworkCapability = []() {
  lunaredu::lunaros::DeviceCapabilityDescriptor descriptor;
  descriptor.id = "network_service";
  descriptor.presence_capabilities = kNetworkPresenceTags;
  descriptor.presence_capability_count =
      sizeof(kNetworkPresenceTags) / sizeof(kNetworkPresenceTags[0]);
  return descriptor;
}();

} // 匿名命名空间

namespace lunaredu::lunaros {

const DeviceCapabilityDescriptor &network_service_capability_descriptor() {
  return kNetworkCapability;
}

bool set_network_capability_available(DeviceCapabilityRegistry &capabilities, bool available, std::string &error) {
  error.clear();
  if (!available) {
    (void)capabilities.deactivate(
        network_service_capability_descriptor().id);
    return true;
  }
  return capabilities.activate(network_service_capability_descriptor(),
                                    &error);
}

} // namespace lunaredu::lunaros
