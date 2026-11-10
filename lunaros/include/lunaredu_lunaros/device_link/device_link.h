#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace lunaredu::lunaros {

struct ServiceNode;

struct DeviceLinkObserver;
using DeviceLinkStatusLedSetter = bool (*)(bool enabled);
using DeviceLinkCloudMaintenanceGate = bool (*)(bool entering);

struct DeviceLinkConfig {
  std::string board_type;
  std::string model;
  std::string production_base_url;
  std::string development_base_url;
  // Legacy override used only by products that still expose the physical
  // pairing flow. Open-claim products leave this empty.
  std::string fixed_pairing_code;
  // Self-enroll and remain discoverable while unbound. The first authenticated
  // Tools account that claims the device becomes the owner.
  bool open_claim = false;
  std::vector<std::string> capabilities;
  // Optional product-owned protocol projection; immutable after composition.
  const DeviceLinkObserver *observer = nullptr;
  DeviceLinkStatusLedSetter set_status_led = nullptr;
  // Releases product-owned realtime resources only for explicit low-memory
  // TLS recovery and large cloud maintenance jobs. Routine admitted polling
  // must run without entering this gate.
  DeviceLinkCloudMaintenanceGate cloud_maintenance_gate = nullptr;
};

enum class DeviceLinkBindingState : uint8_t {
  Connecting,
  Unbound,
  Pairing,
  Bound,
  Error,
};

enum class DeviceLinkEnvironment : uint8_t {
  Production,
  Development,
};

enum class DeviceLinkPlatformConnectionState : uint8_t {
  Disconnected,
  Connected,
  Error,
};

struct DeviceLinkPlatformConnectionSnapshot {
  DeviceLinkPlatformConnectionState state =
      DeviceLinkPlatformConnectionState::Disconnected;
  int status_code = -1;
  std::string error;
  uint32_t last_activity_ms = 0;
  uint32_t sequence = 0;
};

struct DeviceLinkPairingSnapshot {
  uint32_t revision = 0;
  DeviceLinkBindingState state = DeviceLinkBindingState::Unbound;
  std::string code;
  std::string bind_url;
  std::string error;
  std::string account_id;
  std::string account_display_name;
  std::string account_avatar_url;
  bool account_can_manage_hardware = false;
  int64_t expires_at_ms = 0;
  int binding_epoch = 0;
};

// Product composition supplies immutable board-specific facts before service
// installation. LunarOS owns startup, scheduling and network interaction.
bool configure_device_link_runtime(const DeviceLinkConfig &config,
                                   std::string &error);
ServiceNode device_link_service_node();

// Blocking website requests are submitted to LunarOS netio. DeviceLink does
// not create a FreeRTOS task of its own.
void tick_device_link_runtime();

// 屏幕触摸入口：在线且未绑定时创建一个 10 分钟配对会话。
bool request_device_link_pairing();
bool cancel_device_link_pairing();
// 设备端经二次确认后异步解除当前账号绑定；不会清除设备网络凭据。
bool request_device_link_unbind();
enum class DeviceLinkUnbindState : uint8_t { Idle, Pending, Succeeded, Failed };
DeviceLinkUnbindState device_link_unbind_state();
DeviceLinkPairingSnapshot device_link_pairing_snapshot();

// Refresh the short-lived official firmware download ticket on lunar.netio,
// then queue the verified OTA pull. Intended for the on-device update action.
bool request_device_link_firmware_update(std::string &error);

// Platform transport is deliberately independent from account binding. A
// completed 401/403/409 response means the server was reached but rejected the
// operation, so it is reported as Error rather than Disconnected.
DeviceLinkPlatformConnectionSnapshot
device_link_platform_connection_snapshot();

// The official service is the safe default. Environment changes are queued
// onto the DeviceLink owner and persisted only after its current request has
// completed. Callers then explicitly confirm the controlled reboot so no
// in-flight request can cross server boundaries.
DeviceLinkEnvironment device_link_environment();
const char *device_link_environment_name();
const char *device_link_platform_base_url();
bool request_device_link_environment(DeviceLinkEnvironment environment,
                                     std::string &error);
bool device_link_environment_restart_pending();
bool request_device_link_environment_restart(std::string &error);

// 物理维护入口：异步请求一个最长 5 分钟的局域网发现窗口。时钟同步和
// 首次发布由 lunar.netio 执行；绑定不依赖它。
bool start_device_link_lan_discovery(uint32_t duration_ms = 5 * 60 * 1000);

} // namespace lunaredu::lunaros
