#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string>
#include <array>

namespace lunaredu::lunaros {

// 设备最多保存 5 个用户提供的 Wi-Fi 凭据。Network 的运行时兜底网络不占保存名额。
// 这是 device_config 模块自己的公开容量，避免公共头文件依赖项目级 app_config。
constexpr uint8_t kDeviceWifiCredentialMax = 5;
constexpr size_t kDeviceWifiSsidMax = 32;
// Versioned record capacity includes legacy 64-byte values. New requests
// use the shared 8..63-byte passphrase or open-network admission contract.
constexpr size_t kDeviceWifiPasswordMax = 64;
constexpr size_t kDeviceNameMax = 64;
constexpr size_t kDeviceProvisionApNameMax = 32;
constexpr size_t kDeviceCloudEnvironmentMax = 32;
constexpr size_t kDeviceCloudCredentialMax = 1024;
constexpr size_t kDeviceCloudBaseUrlMax = 256;

// 一组 Wi-Fi 名称和密码。
struct DeviceWifiCredential {
  std::string ssid;
  std::string password;
};

// 设备当前保存的 Wi-Fi 配置。首次使用或恢复出厂设置后允许
// configured=false 且无任何凭据。
struct DeviceWifiConfig {
  DeviceWifiCredential credentials[kDeviceWifiCredentialMax];
  uint8_t credential_count = 0;
  uint8_t last_success_index = 0;
  bool configured = false;
};

// 本地设备身份。UUID 由芯片 MAC 派生，名称用于展示和配网热点命名。
struct LocalDeviceIdentityConfig {
  // Stable UUID exposed by the Local Device API. It is derived from the
  // factory MAC but is not the cloud hardware serial or Assistant client ID.
  std::string local_device_uuid;
  std::string name;
  // AP base name; OS adds a stable identity suffix unless a MAC suffix exists.
  std::string provision_ap_name;
};

// 官网设备连接配置；不承载 Assistant 的 MQTT 会话凭据。
struct DeviceCloudConfig {
  std::string environment;
  std::string credential;
  bool configured = false;
  std::string development_base_url{};
};

// 设备运行配置总快照。
struct DeviceConfig {
  DeviceWifiConfig wifi;
  LocalDeviceIdentityConfig identity;
  DeviceCloudConfig cloud;
};

// Returns an owned, coherent snapshot. It remains valid across later writes;
// no mutable configuration storage or borrowed string pointers escape.
DeviceConfig device_config();
// Owned domain snapshots; no borrowed secret pointers escape the state lock.
DeviceWifiConfig device_wifi_config();
DeviceCloudConfig device_cloud_config();
std::string device_cloud_credential();
uint64_t device_cloud_credential_fingerprint();
bool device_wifi_configured();
struct DeviceNetworkConfig { DeviceWifiConfig wifi; LocalDeviceIdentityConfig identity; };
DeviceNetworkConfig device_network_config();
// Allocation-free, non-secret projection for local selectors/render owners.
struct DeviceWifiNames {
  std::array<std::array<char,kDeviceWifiSsidMax+1>,kDeviceWifiCredentialMax> ssids{};
  uint8_t count=0;
};
DeviceWifiNames device_wifi_names();
LocalDeviceIdentityConfig device_identity_config();

// 获取默认设备名称前缀。
const char *device_config_default_device_base_name();

// 当前设备展示名仍为空、为通用默认前缀，或为该设备首次启动时生成的
// “默认前缀-四位字母”名称时返回 true。产品可据此只迁移系统默认名称，
// 而不覆盖用户明确保存的自定义名称。
bool device_config_name_is_default();

// 获取默认配网热点名称前缀。
const char *device_config_default_provision_ap_base_name();
// Derived SSID, bounded to 32 bytes; uses the same stable suffix as the default device name.
std::string device_config_provision_ap_ssid(const LocalDeviceIdentityConfig &identity);

// SDK-neutral write outcome; never includes credentials or backend error text.
enum class DeviceConfigWriteError { None, NotReady, InvalidValue, EncodeFailed, CommitFailed };
inline const char *device_config_write_error_code(DeviceConfigWriteError value) {
  switch (value) {
    case DeviceConfigWriteError::None: return "";
    case DeviceConfigWriteError::NotReady: return "configuration_not_ready";
    case DeviceConfigWriteError::InvalidValue: return "invalid_configuration_value";
    case DeviceConfigWriteError::EncodeFailed: return "configuration_encode_failed";
    case DeviceConfigWriteError::CommitFailed: return "configuration_commit_failed";
  }
  return "configuration_write_failed";
}

// Exact new credentials: 1..5 unique nonempty SSIDs, open or 8..63-byte
// passphrases; no embedded NUL. Invalid, duplicate or excess entries reject the
// complete request without persistence/publication. Values are never trimmed.
bool save_device_wifi_credentials(const DeviceWifiCredential *credentials,
                                  size_t credential_count);
bool save_device_wifi_credentials(const DeviceWifiCredential *credentials,
                                  size_t credential_count, DeviceConfigWriteError &error);

// 清空用户保存的全部 Wi-Fi 凭据。
bool clear_device_wifi_credentials();
bool clear_device_wifi_credentials(DeviceConfigWriteError &error);

// 恢复出厂设置专用：清除所有 Wi-Fi 凭据。该空状态会跨重启保留，
// 以确保设备进入首次配网流程。

// 恢复设备本地配置：清除全部 Wi-Fi，并把展示名和配网热点名恢复为默认值；
// 保留由硬件派生的 UUID；同时清除设备云运行凭据。
bool factory_reset_device_local_config();
bool factory_reset_device_local_config(DeviceConfigWriteError &error);

// 保存网络相关配置，包括 Wi-Fi、设备展示名称和配网热点名称。
bool save_device_network_config(const DeviceWifiCredential *credentials,
                                size_t credential_count,
                                const char *device_name,
                                const char *provision_ap_name);
bool save_device_network_config(const DeviceWifiCredential *credentials,
                                size_t credential_count,
                                const char *device_name,
                                const char *provision_ap_name, DeviceConfigWriteError &error);
// 保存设备展示名和配网热点名，不改写 Wi-Fi/官网设备连接配置。
bool save_device_identity_config(const char *device_name,
                                 const char *provision_ap_name);
bool save_device_identity_config(const char *device_name,
                                 const char *provision_ap_name, DeviceConfigWriteError &error);
bool save_device_cloud_config(const char *environment,
                              const char *credential);
bool save_device_cloud_config(const char *environment,
                              const char *credential, DeviceConfigWriteError &error);
// Atomically publish an explicitly selected server and its fresh identity.
bool save_device_cloud_profile(const char *environment, const char *credential,
                               const std::string &development_base_url);
bool save_device_cloud_profile(const char *environment, const char *credential,
                               const std::string &development_base_url, DeviceConfigWriteError &error);



bool device_cloud_credential_ready(size_t minimum_length);
bool save_device_wifi_last_success_index(uint8_t index);
bool save_device_wifi_last_success_index(uint8_t index, DeviceConfigWriteError &error);

} // namespace lunaredu::lunaros
