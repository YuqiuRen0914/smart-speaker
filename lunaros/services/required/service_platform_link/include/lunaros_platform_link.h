#pragma once
#include "lunaros_platform_link_service_id.h"
#include "lunaros_platform_link_capabilities.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace lunaredu::lunaros {

struct ServiceNode;
struct DeviceApiContext;

struct PlatformLinkObserver;
struct OtaAvailableUpdate;
using PlatformLinkStatusLedSetter = bool (*)(bool enabled);
using PlatformLinkCloudMaintenanceGate = bool (*)(bool entering);

struct PlatformLinkNotification {
  std::string id;
  std::string kind;
  std::string priority;
  std::string title;
  std::string body;
  std::string action_kind;
  std::string action_ref;
};

struct PlatformLinkConfig {
  std::string board_type;
  std::string model;
  std::string production_base_url;
  std::string development_base_url;
  // Products with a single remote Device endpoint may supply its persisted
  // origin here. It is read when a request starts, after product setup.
  std::string (*remote_base_url)() = nullptr;
  // Self-enroll and remain discoverable while unbound. The first authenticated
  // Tools account that claims the device becomes the owner.
  bool open_claim = false;
  // Automatically request and renew a visible pairing code while unbound.
  // Products opt in; interactive pairing remains available without it.
  bool auto_pairing = false;
  // Optional product-declared browser environment for confirmed local handoff.
  std::string setup_browser_origin;
  std::vector<std::string> capabilities;
  // Optional product-owned protocol projection; immutable after composition.
  const PlatformLinkObserver *observer = nullptr;
  PlatformLinkStatusLedSetter set_status_led = nullptr;
  // Releases product-owned realtime resources only for explicit low-memory
  // TLS recovery and large cloud maintenance jobs. Routine admitted polling
  // must run without entering this gate.
  PlatformLinkCloudMaintenanceGate cloud_maintenance_gate = nullptr;
  // Safety-critical products may expose identity/presence/binding without
  // enabling cloud commands (including reboot) or cloud-triggered firmware.
  bool remote_commands_enabled = true;
  // Only products with a dead-man teleop implementation establish this
  // always-on, non-durable MQTT control channel.
  bool realtime_teleop_enabled = false;
  // The product may revoke its own realtime outputs when the channel is lost.
  // This callback must not start network work or block the Link service.
  void (*on_realtime_channel_lost)() = nullptr;
  bool firmware_updates_enabled = true;
  bool role_firmware_updates_only = false;
  // An installer may own an explicit resource plan while still observing the
  // authenticated cloud manifest. Disable only generic automatic requests.
  bool managed_assets_enabled = true;
  // Product-owned preference. Mandatory releases and explicit administrator
  // assignments bypass it. The callback must not block or start network work.
  bool (*auto_install_optional_firmware)(const OtaAvailableUpdate &) = nullptr;
};

// A Boot resumes only the exact business Gateway origin persisted by a cloud
// maintenance intent. This returns no download ticket or device credential.
std::string platform_link_pending_ota_origin();

enum class PlatformLinkBindingState : uint8_t {
  Connecting,
  Unbound,
  Pairing,
  Bound,
  Error,
};

enum class PlatformLinkEnvironment : uint8_t {
  Production,
  Development,
};

enum class PlatformLinkPlatformConnectionState : uint8_t {
  Disconnected,
  Connected,
  Error,
};

struct PlatformLinkPlatformConnectionSnapshot {
  PlatformLinkPlatformConnectionState state =
      PlatformLinkPlatformConnectionState::Disconnected;
  int status_code = -1;
  std::string error;
  // Fixed diagnostic categories only; never server bodies, URLs or credentials.
  std::string operation, error_code;
  uint32_t last_activity_ms = 0;
  uint32_t sequence = 0;
};

struct PlatformLinkPairingSnapshot {
  uint32_t revision = 0;
  PlatformLinkBindingState state = PlatformLinkBindingState::Unbound;
  std::string code;
  std::string bind_url;
  std::string error;
  // Cloud state was accepted, but its single durable record still needs a commit.
  bool persistence_pending = false;
  // A durable local revocation keeps its old epoch until conditional remote
  // release succeeds. It never retains account authority while offline.
  bool revocation_requested = false;
  bool binding_claim_required = false;
  std::string account_id;
  std::string account_display_name;
  std::string account_avatar_url;
  bool account_can_manage_hardware = false;
  // Account-owned resource metadata from presence. Products decide whether
  // their display can consume the advertised format and dimensions.
  bool account_avatar_resource_available = false;
  std::string account_avatar_resource_revision;
  std::string account_avatar_resource_url;
  std::string account_avatar_resource_format;
  uint16_t account_avatar_resource_width = 0;
  uint16_t account_avatar_resource_height = 0;
  size_t account_avatar_resource_maximum_bytes = 0;
  int64_t expires_at_ms = 0;
  int binding_epoch = 0;
  // Shared tickets fail closed until an authenticated presence synchronises them.
  int access_epoch = -1;
  std::string control_manager_id;
  bool shared_control_enabled = false;
};

// Local factory reset: stop cloud work, revoke private access and commit an empty
// binding. This does not send a server unbind request. Failure must abort reset.
bool reset_platform_link_local_binding(std::string &error);

// Product composition supplies immutable board-specific facts before service
// installation. LunarOS owns startup, scheduling and network interaction.
bool configure_platform_link_runtime(DeviceApiContext &runtime, const PlatformLinkConfig &config,
                                   std::string &error);
ServiceNode platform_link_service_node(DeviceApiContext &runtime);

// Blocking website requests are submitted to LunarOS netio. PlatformLink does
// not create a FreeRTOS task of its own.
void tick_platform_link_runtime();

// 屏幕触摸入口：在线且未绑定时创建一个 10 分钟配对会话。
bool request_platform_link_pairing();
// An explicit pairing job is queued/running; platform reconnection is separate.
bool platform_link_pairing_pending();
// Rotate an active pairing code while keeping the current code visible until
// the replacement arrives from the server.
bool refresh_platform_link_pairing();
bool cancel_platform_link_pairing();
// Confirmed local revocation persists immediately, including while offline.
// Remote release converges in the background; network credentials remain.
bool request_platform_link_unbind();
enum class PlatformLinkUnbindState : uint8_t { Idle, Pending, Succeeded, Failed };
PlatformLinkUnbindState platform_link_unbind_state();
PlatformLinkPairingSnapshot platform_link_pairing_snapshot();
// Coherent, allocation-free facts for frequent lifecycle/demand decisions.
// Account presentation and credentials remain in their own richer contracts.
struct PlatformLinkBindingStatus {
  PlatformLinkBindingState state = PlatformLinkBindingState::Unbound;
  bool persistence_pending = false;
  bool revocation_requested = false;
  int binding_epoch = 0;
  uint32_t revision = 0;
  bool bound() const {return state==PlatformLinkBindingState::Bound;}
  bool committed() const {return bound()&&!persistence_pending&&!revocation_requested;}
};
PlatformLinkBindingStatus platform_link_binding_status();
// False for the static/default Unbound state, missing/mismatched/damaged
// binding cache, or an uncommitted authority transition. A persisted local
// Unbound stays ready while remote revocation converges; no account authority
// survives that revocation. Matching committed caches support offline maintenance.
bool platform_link_local_authority_ready();
// Local LAN management is independent of Passport/cloud readiness. Only a
// persisted never-bound migration or confirmed local Unbound record opens it.
bool platform_link_local_management_access_required();
// Narrow UI projection excludes account/avatar/resource metadata.
struct PlatformLinkPairingPresentation {
  PlatformLinkBindingStatus binding;
  std::string code, bind_url, error;
  int64_t expires_at_ms=0;
};
PlatformLinkPairingPresentation platform_link_pairing_presentation();
// Display-only owned cache. No manager/access authority is projected here.
struct PlatformLinkUiSnapshot : PlatformLinkPairingPresentation {
  std::string account_id,account_display_name;
  bool account_avatar_resource_available=false,valid=false;
  std::string account_avatar_resource_revision,account_avatar_resource_url,account_avatar_resource_format;
  uint16_t account_avatar_resource_width=0,account_avatar_resource_height=0;
};
bool platform_link_ui_snapshot_into(PlatformLinkUiSnapshot &);
// Coherent account + binding epoch key for account-scoped local consumers.
// Empty until binding is committed, and immediately empty on revocation.
// Copies no presentation, resource metadata, pairing code or credential.
std::string platform_link_account_identity();
struct PlatformLinkAccountScope {
  std::string identity;
  int binding_epoch=0;
};
// One locked committed observation, without copying account presentation,
// pairing codes or resources. Identity and epoch must never be read separately.
PlatformLinkAccountScope platform_link_account_scope();
// Remaining fraction of an active code's expiry window, regardless of automatic
// renewal policy; -1 when unavailable and 0 after expiry.
float platform_link_pairing_remaining_fraction(uint32_t current_ms);

// Refresh the short-lived official firmware download ticket on lunar.netio,
// then queue the verified OTA pull. Intended for the on-device update action.
bool request_platform_link_firmware_update(std::string &error);
// Check the server on lunar.netio without scheduling an OTA download.
enum class PlatformLinkFirmwareCheckState : uint8_t { Idle, Pending, Succeeded, Failed };
bool request_platform_link_firmware_check(std::string &error);
PlatformLinkFirmwareCheckState platform_link_firmware_check_state();

// Platform transport is deliberately independent from account binding. A
// completed 401/403/409 response means the server was reached but rejected the
// operation, so it is reported as Error rather than Disconnected.
PlatformLinkPlatformConnectionSnapshot
platform_link_platform_connection_snapshot();
PlatformLinkPlatformConnectionState platform_link_platform_connection_state();

// The official service is the safe default. Environment changes are queued
// onto the PlatformLink owner and persisted only after its current request has
// completed. Callers then explicitly confirm the controlled reboot so no
// in-flight request can cross server boundaries.
PlatformLinkEnvironment platform_link_environment();
std::string platform_link_platform_base_url();
std::string platform_link_binding_browser_origin();
bool platform_link_uses_remote_base_url();
bool request_platform_link_environment(PlatformLinkEnvironment environment,
                                     std::string &error,
                                     const std::string &development_base_url = {});
bool platform_link_environment_switch_pending();
bool platform_link_environment_restart_pending();
bool request_platform_link_environment_restart(std::string &error);

// 物理维护入口：异步请求一个最长 5 分钟的局域网发现窗口。时钟同步和
// 首次发布由 lunar.netio 执行；绑定不依赖它。
bool start_platform_link_lan_discovery(uint32_t duration_ms = 5 * 60 * 1000);

} // namespace lunaredu::lunaros
