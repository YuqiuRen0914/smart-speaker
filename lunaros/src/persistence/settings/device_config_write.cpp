#include "device_config_mutation.h"

#include <string>
#include <utility>

#include "lunaredu_lunaros/kernel/log.h"
#include "persistence/settings/nvs_store.h"

namespace lunaredu::lunaros {

bool save_device_wifi_credentials(const DeviceWifiCredential *credentials,
                                  size_t credential_count) {
  StaticLockGuard write_lock(device_config_write_mutex());
  DeviceConfig next = device_config();
  if (!nvs_store_ready()) {
    LOGE("device_config", "保存 Wi-Fi 凭据失败：NVS 尚未就绪");
    return false;
  }

  DeviceWifiConfig next_wifi;
  if (!build_device_wifi_config(credentials, credential_count, 0, next_wifi)) {
    LOGE("device_config", "保存 Wi-Fi 凭据失败：凭据无效");
    return false;
  }

  next.wifi = next_wifi;
  if (!commit_device_config(next)) {
    return false;
  }

  ESP_LOGD("device_config", "已保存 Wi-Fi 凭据：数量=%u",
           static_cast<unsigned>(next.wifi.credential_count));
  return true;
}

bool clear_device_wifi_credentials() {
  StaticLockGuard write_lock(device_config_write_mutex());
  DeviceConfig next = device_config();
  if (!nvs_store_ready()) {
    LOGE("device_config", "清空 Wi-Fi 凭据失败：NVS 尚未就绪");
    return false;
  }

  next.wifi = {};
  if (!commit_device_config(next)) {
    return false;
  }

  ESP_LOGD("device_config", "已清空 Wi-Fi 凭据");
  return true;
}

bool factory_reset_device_local_config() {
  StaticLockGuard write_lock(device_config_write_mutex());
  DeviceConfig next = device_config();
  if (!nvs_store_ready()) {
    LOGE("device_config", "恢复本地设备配置失败：NVS 尚未就绪");
    return false;
  }

  next.wifi = DeviceWifiConfig{};
  next.cloud = {};
  next.identity.name = build_default_device_name(
      next.identity.local_device_uuid);
  next.identity.provision_ap_name =
      default_provision_ap_base_name();
  if (!commit_device_config(next)) {
    return false;
  }

  ESP_LOGD("device_config",
           "已恢复首次使用状态，保留硬件 UUID，清除 Wi-Fi 和云凭据");
  return true;
}

bool save_device_network_config(const DeviceWifiCredential *credentials,
                                size_t credential_count,
                                const char *device_name,
                                const char *provision_ap_name) {
  StaticLockGuard write_lock(device_config_write_mutex());
  DeviceConfig next = device_config();
  if (!nvs_store_ready()) {
    LOGE("device_config", "保存网络配置失败：NVS 尚未就绪");
    return false;
  }

  DeviceWifiConfig next_wifi;
  if (!build_device_wifi_config(credentials, credential_count, 0, next_wifi)) {
    LOGE("device_config", "保存网络配置失败：Wi-Fi 凭据无效");
    return false;
  }

  const std::string next_device_name =
      device_name ? text::trim_copy(std::string(device_name)) : std::string();
  const std::string next_ap_name =
      provision_ap_name ? text::trim_copy(std::string(provision_ap_name))
                        : std::string();
  if (next_device_name.length() > kDeviceNameMax ||
      next_ap_name.length() > kDeviceProvisionApNameMax) {
    LOGE("device_config", "保存网络配置失败：设备身份信息无效");
    return false;
  }

  next.wifi = next_wifi;
  next.identity.name =
      next_device_name.empty()
          ? build_default_device_name(
                next.identity.local_device_uuid)
          : next_device_name;
  next.identity.provision_ap_name =
      next_ap_name.empty() ? default_provision_ap_base_name() : next_ap_name;

  if (!commit_device_config(next)) {
    return false;
  }

  ESP_LOGD("device_config", "已保存网络配置：Wi-Fi=%u 组 设备ID=%s",
           static_cast<unsigned>(next.wifi.credential_count),
           next.identity.local_device_uuid.c_str());
  return true;
}
bool save_device_identity_config(const char *device_name,
                                 const char *provision_ap_name) {
  StaticLockGuard write_lock(device_config_write_mutex());
  DeviceConfig next = device_config();
  if (!nvs_store_ready()) {
    LOGE("device_config", "save identity failed: nvs_not_ready");
    return false;
  }

  const std::string next_device_name =
      device_name ? text::trim_copy(std::string(device_name))
                  : next.identity.name;
  const std::string next_ap_name =
      provision_ap_name ? text::trim_copy(std::string(provision_ap_name))
                        : next.identity.provision_ap_name;
  if (next_device_name.length() > kDeviceNameMax || next_ap_name.empty() ||
      next_ap_name.length() > kDeviceProvisionApNameMax) {
    LOGE("device_config", "save identity failed: invalid_identity");
    return false;
  }

  next.identity.name = next_device_name.empty()
                                    ? build_default_device_name(
                                          next.identity.local_device_uuid)
                                    : next_device_name;
  next.identity.provision_ap_name = next_ap_name;

  if (!commit_device_config(next)) {
    return false;
  }

  ESP_LOGD("device_config", "saved identity name=%s ap=%s",
           next.identity.name.c_str(),
           next.identity.provision_ap_name.c_str());
  return true;
}

bool save_device_cloud_config(const char *environment,
                              const char *credential) {
  StaticLockGuard write_lock(device_config_write_mutex());
  DeviceConfig next = device_config();
  if (!nvs_store_ready()) {
    LOGE("device_config", "保存官网设备连接配置失败：NVS 尚未就绪");
    return false;
  }

  const std::string next_environment =
      environment ? text::trim_copy(std::string(environment))
                  : next.cloud.environment;
  const std::string next_credential =
      credential ? text::trim_copy(std::string(credential))
                 : next.cloud.credential;
  if (next_environment.empty() || next_credential.empty() ||
      next_environment.length() > kDeviceCloudEnvironmentMax ||
      next_credential.length() > kDeviceCloudCredentialMax) {
    LOGE("device_config", "保存官网设备连接配置失败：环境或凭据无效");
    return false;
  }

  if (next.cloud.configured &&
      next.cloud.environment == next_environment &&
      next.cloud.credential == next_credential) {
    return true;
  }

  next.cloud.environment = next_environment;
  next.cloud.credential = next_credential;
  next.cloud.configured = true;

  if (!commit_device_config(next)) {
    LOGE("device_config", "保存官网设备连接配置失败：%s",
         nvs_store_last_error());
    return false;
  }

  ESP_LOGD("device_config", "saved cloud environment=%s",
           next.cloud.environment.c_str());
  return true;
}

bool clear_saved_device_cloud_config() {
  StaticLockGuard write_lock(device_config_write_mutex());
  DeviceConfig next = device_config();
  if (!nvs_store_ready()) {
    LOGE("device_config", "清空官网设备连接配置失败：NVS 尚未就绪");
    return false;
  }

  next.cloud = {};
  if (!commit_device_config(next)) {
    LOGE("device_config", "清空官网设备连接配置失败：%s",
         nvs_store_last_error());
    return false;
  }

  ESP_LOGD("device_config", "已清空官网设备连接配置");
  return true;
}

} // namespace lunaredu::lunaros
