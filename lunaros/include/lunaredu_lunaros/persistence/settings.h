#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string>

namespace lunaredu::lunaros {

struct ServiceNode;

// Generic persistent settings facade. Callers never depend on NVS handles,
// namespaces, or ESP-IDF error types.
std::string settings_get_string(const char *name_space, const char *key,
                                const char *default_value = "");
bool settings_set_string(const char *name_space, const char *key,
                         const std::string &value);
bool settings_get_bool(const char *name_space, const char *key,
                       bool default_value);
bool settings_set_bool(const char *name_space, const char *key, bool value);
bool settings_erase(const char *name_space, const char *key);

ServiceNode settings_service_node();

// 设备最多保存 5 个用户提供的 Wi-Fi 凭据。固件不内置或追加默认网络。
// 这是 device_config 模块自己的公开容量，避免公共头文件依赖项目级 app_config。
constexpr uint8_t kDeviceWifiCredentialMax = 5;
constexpr size_t kDeviceWifiSsidMax = 32;
constexpr size_t kDeviceWifiPasswordMax = 64;
constexpr size_t kDeviceNameMax = 64;
constexpr size_t kDeviceProvisionApNameMax = 32;
constexpr size_t kDeviceCloudEnvironmentMax = 32;
constexpr size_t kDeviceCloudCredentialMax = 1024;

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
  // factory MAC but is not the cloud hardware serial or XiaoZhi client ID.
  std::string local_device_uuid;
  std::string name;
  std::string provision_ap_name;
};

// 官网设备连接配置；不承载 XiaoZhi 的 MQTT 会话凭据。
struct DeviceCloudConfig {
  std::string environment;
  std::string credential;
  bool configured = false;
};

// 设备运行配置总快照。
struct DeviceConfig {
  DeviceWifiConfig wifi;
  LocalDeviceIdentityConfig identity;
  DeviceCloudConfig cloud;
};

// 初始化设备配置服务，从 NVS 读取 / 写入运行配置。
bool setup_device_config_service();

// Returns an owned, coherent snapshot. It remains valid across later writes;
// no mutable configuration storage or borrowed string pointers escape.
DeviceConfig device_config();

// Allocation-free readiness query for the periodic Device Link scheduler.
bool device_cloud_credential_ready(size_t minimum_length);

// 获取默认设备名称前缀。
const char *device_config_default_device_base_name();

// 当前设备展示名仍为空、为通用默认前缀，或为该设备首次启动时生成的
// “默认前缀-四位字母”名称时返回 true。产品可据此只迁移系统默认名称，
// 而不覆盖用户明确保存的自定义名称。
bool device_config_name_is_default();

// 获取默认配网热点名称前缀。
const char *device_config_default_provision_ap_base_name();

// 保存 Wi-Fi 凭据；空 SSID 会被忽略，全部为空时保存失败。
bool save_device_wifi_credentials(const DeviceWifiCredential *credentials,
                                  size_t credential_count);

// 清空用户保存的全部 Wi-Fi 凭据。
bool clear_device_wifi_credentials();

// 恢复出厂设置专用：清除所有 Wi-Fi 凭据。该空状态会跨重启保留，
// 以确保设备进入首次配网流程。

// 恢复设备本地配置：清除全部 Wi-Fi，并把展示名和配网热点名恢复为默认值；
// 保留由硬件派生的 UUID；同时清除设备云运行凭据。
bool factory_reset_device_local_config();

// 保存网络相关配置，包括 Wi-Fi、设备展示名称和配网热点名称。
bool save_device_network_config(const DeviceWifiCredential *credentials,
                                size_t credential_count,
                                const char *device_name,
                                const char *provision_ap_name);
// 保存设备展示名和配网热点名，不改写 Wi-Fi/官网设备连接配置。
bool save_device_identity_config(const char *device_name,
                                 const char *provision_ap_name);
bool save_device_cloud_config(const char *environment,
                              const char *credential);

bool clear_saved_device_cloud_config();

// 记录最近一次成功连接的用户 Wi-Fi 凭据索引。
bool save_device_wifi_last_success_index(uint8_t index);

} // namespace lunaredu::lunaros
