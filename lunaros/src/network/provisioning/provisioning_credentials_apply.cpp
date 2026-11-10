#include "network/provisioning/provisioning_credentials.h"

#include <string>

#include "lunaredu_lunaros/kernel/log.h"
#include "lunaredu_lunaros/network/network_commands.h"
#include "network/provisioning/provisioning_driver.h"

namespace lunaredu::lunaros {

NetworkProvisioningCredentialResult apply_provisioning_wifi_credentials(
    const char *ssid, const char *password) {
  DeviceWifiCredential submitted;
  submitted.ssid = ssid ? std::string(ssid) : std::string();
  submitted.password = password ? std::string(password) : std::string();
  if (submitted.ssid.empty()) {
    return NetworkProvisioningCredentialResult::MissingSsid;
  }

  DeviceWifiCredential credentials[kDeviceWifiCredentialMax];
  size_t credential_count = 0;
  {
    const auto config = device_config();
    credential_count = build_provisioning_wifi_credentials(
        submitted, config.wifi.credentials, config.wifi.credential_count,
        credentials, kDeviceWifiCredentialMax);
  }
  if (!save_device_wifi_credentials(credentials, credential_count)) {
    LOGW("provision", "保存 Wi-Fi 凭据失败：数量=%u",
         static_cast<unsigned>(credential_count));
    return NetworkProvisioningCredentialResult::SaveFailed;
  }

  const DeviceConfig &persisted_config = device_config();
  if (persisted_config.wifi.credential_count == 0 ||
      persisted_config.wifi.credentials[0].ssid != submitted.ssid) {
    LOGE("provision",
         "Wi-Fi 凭据保存后校验失败：SSID=%s 首选SSID=%s 数量=%u",
         submitted.ssid.c_str(),
         persisted_config.wifi.credential_count == 0
             ? "-"
             : persisted_config.wifi.credentials[0].ssid.c_str(),
         static_cast<unsigned>(persisted_config.wifi.credential_count));
    return NetworkProvisioningCredentialResult::SaveFailed;
  }
  LOGI("provision", "Wi-Fi 凭据已保存并校验：SSID=%s 数量=%u",
       submitted.ssid.c_str(),
       static_cast<unsigned>(persisted_config.wifi.credential_count));

  uint8_t submitted_index = 0;
  for (uint8_t index = 0; index < persisted_config.wifi.credential_count;
       ++index) {
    if (persisted_config.wifi.credentials[index].ssid == submitted.ssid) {
      submitted_index = index;
      break;
    }
  }

  if (provisioning_driver_station_suspended()) {
    provisioning_driver_set_station_suspended(false);
  }

  std::string error;
  if (!lunaredu::lunaros::request_network_apply_saved_credential(
          submitted_index, error)) {
    LOGW("provision", "提交 Wi-Fi 应用请求失败：SSID=%s error=%s",
         submitted.ssid.c_str(), error.c_str());
    return NetworkProvisioningCredentialResult::ApplyFailed;
  }

  LOGI("provision", "Wi-Fi 凭据已进入应用队列：SSID=%s index=%u",
       submitted.ssid.c_str(), static_cast<unsigned>(submitted_index));

  return NetworkProvisioningCredentialResult::Ok;
}

} // namespace lunaredu::lunaros
