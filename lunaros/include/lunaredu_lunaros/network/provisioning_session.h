#pragma once

#include <cstdint>
#include <string>

namespace lunaredu::lunaros {

enum class ProvisioningSessionPhase : uint8_t {
  Idle,
  Discovered,
  LocalSessionAuthenticated,
  WifiProvisioning,
  WifiConnecting,
  PlatformEnrolled,
  AccountPairing,
  Bound,
  LanControl,
  Failed,
  Expired,
};

enum class ProvisioningWifiState : uint8_t {
  Unknown,
  Unconfigured,
  Connecting,
  Connected,
  Failed,
};

enum class ProvisioningSessionError : uint8_t {
  None,
  BootstrapUnsupported,
  BootstrapSessionExpired,
  BootstrapConfirmationRequired,
  BootstrapConfirmationFailed,
  WifiCredentialsInvalid,
  WifiConnectionTimeout,
  WifiAuthFailed,
  DeviceIdentityMismatch,
  DeviceAlreadyBound,
  DeviceRetired,
  PlatformEnrollmentFailed,
  PairingExpired,
  TransportDisconnected,
  InvalidTransition,
};

enum class ProvisioningSessionEventType : uint8_t {
  OpenSession,
  Confirmed,
  WifiSubmitted,
  ProvisioningStatus,
  PlatformEnrolled,
  PairingStarted,
  Bound,
  LanControl,
  Failed,
};

struct ProvisioningSessionSnapshot {
  ProvisioningSessionPhase phase = ProvisioningSessionPhase::Idle;
  ProvisioningWifiState wifi_state = ProvisioningWifiState::Unknown;
  ProvisioningSessionError error = ProvisioningSessionError::None;
  uint32_t session_expires_at_ms = 0;
};

struct ProvisioningSessionEvent {
  ProvisioningSessionEventType type =
      ProvisioningSessionEventType::OpenSession;
  ProvisioningWifiState wifi_state = ProvisioningWifiState::Unknown;
  ProvisioningSessionError error = ProvisioningSessionError::None;
  uint32_t session_expires_at_ms = 0;
};

ProvisioningSessionSnapshot create_provisioning_session(
    uint32_t session_expires_at_ms);

bool provisioning_session_expired(const ProvisioningSessionSnapshot &snapshot,
                                  uint32_t now_ms);

bool expire_provisioning_session(ProvisioningSessionSnapshot &snapshot,
                                 uint32_t now_ms);

bool transition_provisioning_session(ProvisioningSessionSnapshot &snapshot,
                                     const ProvisioningSessionEvent &event,
                                     std::string &error);

const char *provisioning_session_phase_name(ProvisioningSessionPhase phase);
const char *provisioning_wifi_state_name(ProvisioningWifiState state);
const char *provisioning_session_error_name(ProvisioningSessionError error);

bool provisioning_session_phase_terminal(ProvisioningSessionPhase phase);

}  // namespace lunaredu::lunaros

