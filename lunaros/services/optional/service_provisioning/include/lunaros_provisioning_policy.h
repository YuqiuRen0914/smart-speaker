#pragma once
#include <cstdint>
#include <string>
// Pure composition values. Provisioning owns execution and state; consumers
// may declare this policy without selecting or depending on that service.
namespace lunaredu::lunaros {
inline constexpr char kProvisioningDefaultPassword[] = "12345678";
inline constexpr uint32_t kProvisioningFailureTimeoutMs = 60000;
enum class ProvisioningMode : uint8_t {
  Disabled,
  SoftAp,
  SoftApBle,
  Direct,
};

struct ProvisioningPolicy {
  uint32_t window_timeout_ms = 300000;
  uint32_t retry_cooldown_ms = 60000;
  bool enabled = true;
  ProvisioningMode mode = ProvisioningMode::SoftAp;
  bool start_when_credentials_missing = true;
  bool start_when_connection_times_out = true;
  // Automatic provisioning keeps saved-network retries alive for unattended
  // devices. Products may explicitly suspend STA for an interactive window.
  bool suspend_station_while_provisioning = false;
  bool stop_when_connected = true;
  // Zero uses the shared 60-second timeout, without a minimum retry count.
  uint32_t reconnect_failure_timeout_ms = 0;
  uint32_t reconnect_failure_min_attempts = 0;
  // Shared default also replaces an older persisted random password at boot.
  // Products may override with exactly 8 digits; empty opts into a per-device
  // generated/persisted password.
  std::string softap_password = kProvisioningDefaultPassword;
};

} // namespace lunaredu::lunaros
