#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "lunaredu_lunaros/model/device_model.h"
#include "lunaredu_lunaros/network/espnow.h"
#include "lunaredu_lunaros/network/network_jobs.h"
namespace lunaredu::lunaros {

inline constexpr size_t kNetworkInitialWifiCredentialCapacity = 5;
inline constexpr size_t kNetworkWifiScanCapacity = 24;
inline constexpr size_t kNetworkWifiSsidCapacity = 33;
inline constexpr size_t kNetworkWifiScanErrorCapacity = 48;

struct ServiceNode;

inline constexpr uint32_t kNetworkPollIntervalMs = 20;

enum class NetworkLinkState : uint8_t {
  Down,
  Connecting,
  Up,
};

enum class NetworkInternetState : uint8_t {
  Unknown,
  Unavailable,
  Reachable,
};

enum class NetworkProvisioningState : uint8_t {
  Idle,
  Active,
  Applying,
};

enum class NetworkProvisioningMode : uint8_t {
  Disabled,
  SoftAp,
};

enum class NetworkClockState : uint8_t {
  Unsynchronized,
  Synchronizing,
  Valid,
};

enum class NetworkDeviceLinkState : uint8_t {
  Disabled,
  Unbound,
  Connecting,
  Ready,
  Degraded,
};

enum class NetworkWifiScanState : uint8_t {
  Idle,
  Scanning,
  Ready,
  Failed,
};

struct NetworkWifiScanAccessPoint {
  std::array<char, kNetworkWifiSsidCapacity> ssid{};
  int32_t rssi = 0;
  bool secure = false;
  uint8_t authmode = 0;
  uint8_t channel = 0;
};

struct NetworkWifiScanSnapshot {
  uint32_t sequence = 0;
  NetworkWifiScanState state = NetworkWifiScanState::Idle;
  std::array<NetworkWifiScanAccessPoint, kNetworkWifiScanCapacity>
      access_points{};
  size_t access_point_count = 0;
  std::array<char, kNetworkWifiScanErrorCapacity> error{};
};

struct NetworkProvisioningPolicy {
  bool enabled = true;
  NetworkProvisioningMode mode = NetworkProvisioningMode::SoftAp;
  bool start_when_credentials_missing = true;
  bool start_when_connection_times_out = true;
  bool suspend_station_while_provisioning = true;
  bool stop_when_connected = true;
  bool restart_after_submit_success = false;
  uint32_t restart_after_submit_delay_ms = 1500;
  // Zero keeps the conservative shared defaults (180 s / 10 attempts).
  uint32_t reconnect_failure_timeout_ms = 0;
  uint32_t reconnect_failure_min_attempts = 0;
  // Optional product-owned SoftAP password. Empty preserves the shared
  // per-device random password. A supplied value must be exactly 8 digits.
  std::string softap_password;
};

struct NetworkInitialWifiCredential {
  std::string ssid;
  std::string password;
};

enum class NetworkWifiPowerSaveLevel { LOW_POWER, BALANCED, PERFORMANCE };

struct NetworkPolicy {
  NetworkWifiPowerSaveLevel initial_power_save = NetworkWifiPowerSaveLevel::BALANCED;
  NetworkProvisioningPolicy provisioning;
  bool synchronize_clock_on_connect = true;
  bool require_tls_for_production = true;
  NetworkJobPolicy jobs;
  // Optional product-owned bootstrap credentials. LunarOS seeds them only
  // when persistent storage contains no user credentials. General products,
  // including the desktop pet, leave this disabled.
  std::array<NetworkInitialWifiCredential,
             kNetworkInitialWifiCredentialCapacity>
      initial_wifi_credentials{};
  uint8_t initial_wifi_credential_count = 0;
  bool seed_initial_wifi_credentials_when_empty = false;
  // False starts the same Network-owned radio without STA association. Such
  // products must also disable provisioning and clock synchronization.
  bool station_enabled = true;
  EspNowPolicy espnow;
};

struct NetworkSnapshot {
  uint32_t sequence = 0;
  bool driver_bound = false;
  bool service_started = false;
  bool platform_ready = false;
  bool credentials_available = false;
  NetworkLinkState link = NetworkLinkState::Down;
  int32_t rssi = 0;
  NetworkInternetState internet = NetworkInternetState::Unknown;
  NetworkProvisioningState provisioning = NetworkProvisioningState::Idle;
  NetworkClockState clock = NetworkClockState::Unsynchronized;
  NetworkDeviceLinkState device_link = NetworkDeviceLinkState::Disabled;
  std::string local_ip;
  std::string target_ssid;
  std::string provisioning_ssid;
  std::string provisioning_ip;
  std::string last_error;
};

struct NetworkFastSnapshot {
  uint32_t sequence = 0;
  bool credentials_available = false;
  NetworkLinkState link = NetworkLinkState::Down;
  NetworkProvisioningState provisioning = NetworkProvisioningState::Idle;
};

const char *network_link_state_name(NetworkLinkState state);
const char *network_internet_state_name(NetworkInternetState state);
const char *network_provisioning_state_name(NetworkProvisioningState state);
const char *network_clock_state_name(NetworkClockState state);
const char *network_device_link_state_name(NetworkDeviceLinkState state);

// Safe network facts. Driver binding, owner-only transitions, locks and the
// provisioning password are deliberately absent from the public snapshot.
NetworkSnapshot network_snapshot();
NetworkWifiScanSnapshot network_wifi_scan_snapshot();
void network_wifi_scan_snapshot_into(NetworkWifiScanSnapshot &snapshot);
// Allocation-free fast-path for realtime tasks.  The full snapshot owns
// strings and must not be copied from audio/render loops.
NetworkFastSnapshot network_fast_snapshot();
NetworkLinkState network_link_state();
NetworkProvisioningState network_provisioning_state();
NetworkPolicy network_policy();
void poll_network_on_owner();
bool synchronize_network_clock_blocking(uint32_t timeout_ms,
                                        std::string &error,
                                        bool force = false);
void report_network_internet(NetworkInternetState state,
                             const char *error = nullptr);
void report_network_clock(NetworkClockState state,
                          const char *error = nullptr);
void report_network_device_link(NetworkDeviceLinkState state,
                                const char *error = nullptr);

// Explicit local-only secret read for the on-device setup surface. Device API,
// DeviceLink and diagnostics must use NetworkSnapshot and cannot see it.
std::string network_local_provisioning_password();
// Static descriptor and runtime availability gate owned by NetworkService.
// Device API projection registration lives in the Device API component.
const DeviceCapabilityDescriptor &network_service_capability_descriptor();
bool set_network_capability_available(bool available, std::string &error);

// Configure the complete shared network runtime before service registration.
// This resets the service and job manager, binds the ESP32-S3 driver and
// prepares Network's service state as one operation.
bool configure_network_runtime(const NetworkPolicy &policy,
                               const char *timezone, std::string &error);

// Register Network's shared deferred lifecycle. Activation is performed by
// ServiceHost on the fixed lunar.network executor.
ServiceNode network_service_node();

} // namespace lunaredu::lunaros
