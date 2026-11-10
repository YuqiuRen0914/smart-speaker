#include "sdkconfig.h"

#include "lunaredu_lunaros/network/network.h"
#include "lunaredu_lunaros/kernel/log.h"

namespace {

#if CONFIG_LUNAREDU_NETWORK_ENABLE_PROVISIONING_SERVICE
constexpr const char *kNetworkPresenceTags[] = {
    "wifi_sta",
    "wifi_provision_ap",
    "wifi_provision_ble",
};
#else
constexpr const char *kNetworkPresenceTags[] = {
    "wifi_sta",
};
#endif

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

bool set_network_capability_available(bool available, std::string &error) {
  error.clear();
  if (!available) {
    (void)deactivate_device_capability(
        network_service_capability_descriptor().id);
    return true;
  }
  return activate_device_capability(network_service_capability_descriptor(),
                                    &error);
}

} // namespace lunaredu::lunaros
