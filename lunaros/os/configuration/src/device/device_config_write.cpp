#include "device_config_mutation.h"

#include <string>
#include <utility>

#include <esp_log.h>
#include "storage/nvs_backend.h"

namespace lunaredu::lunaros {
namespace {
bool valid_identity_text(const std::string &value,size_t limit,bool empty=false) {
  if((!empty && value.empty()) || value.size()>limit) return false;
  for(unsigned char c:value) if(c<0x20 || c==0x7f) return false;
  return true;
}
}

bool save_device_wifi_credentials(const DeviceWifiCredential *credentials,
                                  size_t credential_count, DeviceConfigWriteError &error) {
  error = DeviceConfigWriteError::None;
  StaticLockGuard write_lock(device_config_write_mutex());
  DeviceConfig next = device_config();
  if (!nvs_store_ready()) {
    error = DeviceConfigWriteError::NotReady;
    ESP_LOGE("device_config", "保存 Wi-Fi 凭据失败：NVS 尚未就绪");
    if (error == DeviceConfigWriteError::None) error = DeviceConfigWriteError::InvalidValue;
    return false;
  }

  DeviceWifiConfig next_wifi;
  if (!build_device_wifi_config(credentials, credential_count, 0, next_wifi)) {
    ESP_LOGE("device_config", "保存 Wi-Fi 凭据失败：凭据无效");
    if (error == DeviceConfigWriteError::None) error = DeviceConfigWriteError::InvalidValue;
    return false;
  }

  next.wifi = next_wifi;
  if (!commit_device_config(next, &error)) {
    return false;
  }

  ESP_LOGD("device_config", "已保存 Wi-Fi 凭据：数量=%u",
           static_cast<unsigned>(next.wifi.credential_count));
  return true;
}

bool clear_device_wifi_credentials(DeviceConfigWriteError &error) {
  error = DeviceConfigWriteError::None;
  StaticLockGuard write_lock(device_config_write_mutex());
  DeviceConfig next = device_config();
  if (!nvs_store_ready()) {
    error = DeviceConfigWriteError::NotReady;
    ESP_LOGE("device_config", "清空 Wi-Fi 凭据失败：NVS 尚未就绪");
    if (error == DeviceConfigWriteError::None) error = DeviceConfigWriteError::InvalidValue;
    return false;
  }

  next.wifi = {};
  if (!commit_device_config(next, &error)) {
    return false;
  }

  ESP_LOGD("device_config", "已清空 Wi-Fi 凭据");
  return true;
}

bool factory_reset_device_local_config(DeviceConfigWriteError &error) {
  error = DeviceConfigWriteError::None;
  StaticLockGuard write_lock(device_config_write_mutex());
  DeviceConfig next = device_config();
  if (!nvs_store_ready()) {
    error = DeviceConfigWriteError::NotReady;
    ESP_LOGE("device_config", "恢复本地设备配置失败：NVS 尚未就绪");
    if (error == DeviceConfigWriteError::None) error = DeviceConfigWriteError::InvalidValue;
    return false;
  }

  next.wifi = DeviceWifiConfig{};
  next.cloud = {};
  next.identity.name = build_default_device_name(
      next.identity.local_device_uuid);
  next.identity.provision_ap_name =
      default_provision_ap_base_name();
  if (!commit_device_config(next, &error)) {
    return false;
  }

  ESP_LOGD("device_config",
           "已恢复首次使用状态，保留硬件 UUID，清除 Wi-Fi 和云凭据");
  return true;
}

bool save_device_network_config(const DeviceWifiCredential *credentials,
                                size_t credential_count,
                                const char *device_name,
                                const char *provision_ap_name, DeviceConfigWriteError &error) {
  error = DeviceConfigWriteError::None;
  StaticLockGuard write_lock(device_config_write_mutex());
  DeviceConfig next = device_config();
  if (!nvs_store_ready()) {
    error = DeviceConfigWriteError::NotReady;
    ESP_LOGE("device_config", "保存网络配置失败：NVS 尚未就绪");
    if (error == DeviceConfigWriteError::None) error = DeviceConfigWriteError::InvalidValue;
    return false;
  }

  DeviceWifiConfig next_wifi;
  if (!build_device_wifi_config(credentials, credential_count, 0, next_wifi)) {
    ESP_LOGE("device_config", "保存网络配置失败：Wi-Fi 凭据无效");
    if (error == DeviceConfigWriteError::None) error = DeviceConfigWriteError::InvalidValue;
    return false;
  }

  const std::string next_device_name =
      device_name ? text::trim_copy(std::string(device_name)) : std::string();
  const std::string next_ap_name =
      provision_ap_name ? text::trim_copy(std::string(provision_ap_name))
                        : std::string();
  if (!valid_identity_text(next_device_name,kDeviceNameMax,true) ||
      !valid_identity_text(next_ap_name,kDeviceProvisionApNameMax,true)) {
    ESP_LOGE("device_config", "保存网络配置失败：设备身份信息无效");
    if (error == DeviceConfigWriteError::None) error = DeviceConfigWriteError::InvalidValue;
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

  if (!commit_device_config(next, &error)) {
    return false;
  }

  ESP_LOGD("device_config", "已保存网络配置：Wi-Fi=%u 组 设备ID=%s",
           static_cast<unsigned>(next.wifi.credential_count),
           next.identity.local_device_uuid.c_str());
  return true;
}
bool save_device_identity_config(const char *device_name,
                                 const char *provision_ap_name, DeviceConfigWriteError &error) {
  error = DeviceConfigWriteError::None;
  StaticLockGuard write_lock(device_config_write_mutex());
  DeviceConfig next = device_config();
  if (!nvs_store_ready()) {
    error = DeviceConfigWriteError::NotReady;
    ESP_LOGE("device_config", "save identity failed: nvs_not_ready");
    if (error == DeviceConfigWriteError::None) error = DeviceConfigWriteError::InvalidValue;
    return false;
  }

  const std::string next_device_name =
      device_name ? text::trim_copy(std::string(device_name))
                  : next.identity.name;
  const std::string next_ap_name =
      provision_ap_name ? text::trim_copy(std::string(provision_ap_name))
                        : next.identity.provision_ap_name;
  if (!valid_identity_text(next_device_name,kDeviceNameMax,true) ||
      !valid_identity_text(next_ap_name,kDeviceProvisionApNameMax)) {
    ESP_LOGE("device_config", "save identity failed: invalid_identity");
    if (error == DeviceConfigWriteError::None) error = DeviceConfigWriteError::InvalidValue;
    return false;
  }

  next.identity.name = next_device_name.empty()
                                    ? build_default_device_name(
                                          next.identity.local_device_uuid)
                                    : next_device_name;
  next.identity.provision_ap_name = next_ap_name;

  if (!commit_device_config(next, &error)) {
    return false;
  }

  ESP_LOGD("device_config", "saved identity name=%s ap=%s",
           next.identity.name.c_str(),
           next.identity.provision_ap_name.c_str());
  return true;
}

static bool save_cloud_values(const char *environment, const char *credential,
                             const std::string *origin, DeviceConfigWriteError &error) {
  error = DeviceConfigWriteError::None;
  StaticLockGuard write_lock(device_config_write_mutex());
  DeviceConfig next = device_config();
  const std::string development_base_url = origin ? *origin : next.cloud.development_base_url;
  if (!nvs_store_ready()) {
    error = DeviceConfigWriteError::NotReady;
    ESP_LOGE("device_config", "保存官网设备连接配置失败：NVS 尚未就绪");
    if (error == DeviceConfigWriteError::None) error = DeviceConfigWriteError::InvalidValue;
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
      next_credential.length() > kDeviceCloudCredentialMax ||
      development_base_url.size() > kDeviceCloudBaseUrlMax) {
    ESP_LOGE("device_config", "保存官网设备连接配置失败：环境或凭据无效");
    if (error == DeviceConfigWriteError::None) error = DeviceConfigWriteError::InvalidValue;
    return false;
  }

  if (next.cloud.configured &&
      next.cloud.environment == next_environment &&
      next.cloud.credential == next_credential &&
      next.cloud.development_base_url == development_base_url) {
    return true;
  }

  next.cloud.environment = next_environment;
  next.cloud.credential = next_credential;
  next.cloud.configured = true;
  next.cloud.development_base_url = development_base_url;

  if (!commit_device_config(next, &error)) {
    return false;
  }

  ESP_LOGD("device_config", "saved cloud environment=%s",
           next.cloud.environment.c_str());
  return true;
}

bool save_device_cloud_config(const char *environment, const char *credential, DeviceConfigWriteError &error) {
  error = DeviceConfigWriteError::None;
  return save_cloud_values(environment, credential, nullptr, error);
}

bool save_device_cloud_profile(const char *environment, const char *credential,
                               const std::string &development_base_url, DeviceConfigWriteError &error) {
  error = DeviceConfigWriteError::None;
  return save_cloud_values(environment, credential, &development_base_url, error);
}


bool save_device_wifi_credentials(const DeviceWifiCredential *credentials,
                                  size_t credential_count) {
  DeviceConfigWriteError error; return save_device_wifi_credentials(credentials, credential_count, error);
}

bool clear_device_wifi_credentials() {
  DeviceConfigWriteError error; return clear_device_wifi_credentials(error);
}

bool factory_reset_device_local_config() {
  DeviceConfigWriteError error; return factory_reset_device_local_config(error);
}

bool save_device_network_config(const DeviceWifiCredential *credentials,
                                size_t credential_count,
                                const char *device_name,
                                const char *provision_ap_name) {
  DeviceConfigWriteError error; return save_device_network_config(credentials, credential_count, device_name, provision_ap_name, error);
}

bool save_device_identity_config(const char *device_name,
                                 const char *provision_ap_name) {
  DeviceConfigWriteError error; return save_device_identity_config(device_name, provision_ap_name, error);
}

bool save_device_cloud_config(const char *environment, const char *credential) {
  DeviceConfigWriteError error; return save_device_cloud_config(environment, credential, error);
}

bool save_device_cloud_profile(const char *environment, const char *credential,
                               const std::string &development_base_url) {
  DeviceConfigWriteError error; return save_device_cloud_profile(environment, credential, development_base_url, error);
}

} // namespace lunaredu::lunaros
