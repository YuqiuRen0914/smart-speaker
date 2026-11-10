#include "lunaredu_lunaros/network/provisioning_session.h"

#include <initializer_list>

namespace lunaredu::lunaros {
namespace {

bool phase_is(ProvisioningSessionPhase phase,
              std::initializer_list<ProvisioningSessionPhase> allowed) {
  for (const ProvisioningSessionPhase candidate : allowed) {
    if (phase == candidate) return true;
  }
  return false;
}

bool require_phase(const ProvisioningSessionSnapshot &snapshot,
                   std::initializer_list<ProvisioningSessionPhase> allowed,
                   std::string &error) {
  if (phase_is(snapshot.phase, allowed)) return true;
  error = "provisioning_session_invalid_transition";
  return false;
}

}  // namespace

ProvisioningSessionSnapshot create_provisioning_session(
    uint32_t session_expires_at_ms) {
  ProvisioningSessionSnapshot snapshot;
  snapshot.phase = ProvisioningSessionPhase::Discovered;
  snapshot.session_expires_at_ms = session_expires_at_ms;
  return snapshot;
}

bool provisioning_session_expired(const ProvisioningSessionSnapshot &snapshot,
                                  uint32_t now_ms) {
  if (snapshot.session_expires_at_ms == 0) return false;
  return static_cast<int32_t>(now_ms - snapshot.session_expires_at_ms) >= 0;
}

bool expire_provisioning_session(ProvisioningSessionSnapshot &snapshot,
                                 uint32_t now_ms) {
  if (!provisioning_session_expired(snapshot, now_ms)) return false;
  snapshot.phase = ProvisioningSessionPhase::Expired;
  snapshot.error = ProvisioningSessionError::BootstrapSessionExpired;
  snapshot.session_expires_at_ms = 0;
  return true;
}

bool transition_provisioning_session(ProvisioningSessionSnapshot &snapshot,
                                     const ProvisioningSessionEvent &event,
                                     std::string &error) {
  error.clear();

  switch (event.type) {
    case ProvisioningSessionEventType::OpenSession:
      if (!require_phase(snapshot, {ProvisioningSessionPhase::Discovered},
                         error)) {
        return false;
      }
      if (event.session_expires_at_ms == 0) {
        error = "provisioning_session_expiry_required";
        return false;
      }
      snapshot.phase = ProvisioningSessionPhase::LocalSessionAuthenticated;
      snapshot.session_expires_at_ms = event.session_expires_at_ms;
      snapshot.error = ProvisioningSessionError::None;
      return true;

    case ProvisioningSessionEventType::Confirmed:
      if (!require_phase(snapshot,
                         {ProvisioningSessionPhase::LocalSessionAuthenticated},
                         error)) {
        return false;
      }
      snapshot.phase = ProvisioningSessionPhase::WifiProvisioning;
      snapshot.error = ProvisioningSessionError::None;
      return true;

    case ProvisioningSessionEventType::WifiSubmitted:
      if (!require_phase(snapshot,
                         {ProvisioningSessionPhase::WifiProvisioning},
                         error)) {
        return false;
      }
      snapshot.phase = ProvisioningSessionPhase::WifiConnecting;
      snapshot.wifi_state = ProvisioningWifiState::Connecting;
      snapshot.error = ProvisioningSessionError::None;
      return true;

    case ProvisioningSessionEventType::ProvisioningStatus:
      if (event.error != ProvisioningSessionError::None) {
        if (!require_phase(snapshot,
                           {ProvisioningSessionPhase::WifiProvisioning,
                            ProvisioningSessionPhase::WifiConnecting},
                           error)) {
          return false;
        }
        snapshot.phase = ProvisioningSessionPhase::Failed;
        snapshot.wifi_state = event.wifi_state;
        snapshot.error = event.error;
        return true;
      }
      if (event.wifi_state == ProvisioningWifiState::Unconfigured) {
        if (!require_phase(snapshot,
                           {ProvisioningSessionPhase::WifiProvisioning},
                           error)) {
          return false;
        }
        snapshot.error = ProvisioningSessionError::None;
        return true;
      }
      if (event.wifi_state == ProvisioningWifiState::Connecting) {
        if (!require_phase(snapshot,
                           {ProvisioningSessionPhase::WifiProvisioning,
                            ProvisioningSessionPhase::WifiConnecting},
                           error)) {
          return false;
        }
        snapshot.phase = ProvisioningSessionPhase::WifiConnecting;
        snapshot.wifi_state = event.wifi_state;
        snapshot.error = ProvisioningSessionError::None;
        return true;
      }
      if (event.wifi_state == ProvisioningWifiState::Connected) {
        if (!require_phase(snapshot,
                           {ProvisioningSessionPhase::WifiConnecting},
                           error)) {
          return false;
        }
        snapshot.wifi_state = event.wifi_state;
        snapshot.error = ProvisioningSessionError::None;
        return true;
      }
      error = "provisioning_session_invalid_wifi_state";
      return false;

    case ProvisioningSessionEventType::PlatformEnrolled:
      if (!require_phase(snapshot,
                         {ProvisioningSessionPhase::WifiConnecting,
                          ProvisioningSessionPhase::PlatformEnrolled},
                         error)) {
        return false;
      }
      snapshot.phase = ProvisioningSessionPhase::PlatformEnrolled;
      snapshot.error = ProvisioningSessionError::None;
      return true;

    case ProvisioningSessionEventType::PairingStarted:
      if (!require_phase(snapshot,
                         {ProvisioningSessionPhase::PlatformEnrolled},
                         error)) {
        return false;
      }
      snapshot.phase = ProvisioningSessionPhase::AccountPairing;
      snapshot.error = ProvisioningSessionError::None;
      return true;

    case ProvisioningSessionEventType::Bound:
      if (!require_phase(snapshot,
                         {ProvisioningSessionPhase::AccountPairing,
                          ProvisioningSessionPhase::PlatformEnrolled},
                         error)) {
        return false;
      }
      snapshot.phase = ProvisioningSessionPhase::Bound;
      snapshot.error = ProvisioningSessionError::None;
      return true;

    case ProvisioningSessionEventType::LanControl:
      if (!require_phase(snapshot,
                         {ProvisioningSessionPhase::Bound,
                          ProvisioningSessionPhase::PlatformEnrolled},
                         error)) {
        return false;
      }
      snapshot.phase = ProvisioningSessionPhase::LanControl;
      snapshot.error = ProvisioningSessionError::None;
      return true;

    case ProvisioningSessionEventType::Failed:
      snapshot.phase = ProvisioningSessionPhase::Failed;
      snapshot.error = event.error == ProvisioningSessionError::None
                           ? ProvisioningSessionError::PlatformEnrollmentFailed
                           : event.error;
      return true;
  }

  error = "provisioning_session_event_unknown";
  return false;
}

const char *provisioning_session_phase_name(ProvisioningSessionPhase phase) {
  switch (phase) {
    case ProvisioningSessionPhase::Idle:
      return "idle";
    case ProvisioningSessionPhase::Discovered:
      return "discovered";
    case ProvisioningSessionPhase::LocalSessionAuthenticated:
      return "local_session_authenticated";
    case ProvisioningSessionPhase::WifiProvisioning:
      return "wifi_provisioning";
    case ProvisioningSessionPhase::WifiConnecting:
      return "wifi_connecting";
    case ProvisioningSessionPhase::PlatformEnrolled:
      return "platform_enrolled";
    case ProvisioningSessionPhase::AccountPairing:
      return "account_pairing";
    case ProvisioningSessionPhase::Bound:
      return "bound";
    case ProvisioningSessionPhase::LanControl:
      return "lan_control";
    case ProvisioningSessionPhase::Failed:
      return "failed";
    case ProvisioningSessionPhase::Expired:
      return "expired";
  }
  return "unknown";
}

const char *provisioning_wifi_state_name(ProvisioningWifiState state) {
  switch (state) {
    case ProvisioningWifiState::Unknown:
      return "unknown";
    case ProvisioningWifiState::Unconfigured:
      return "unconfigured";
    case ProvisioningWifiState::Connecting:
      return "connecting";
    case ProvisioningWifiState::Connected:
      return "connected";
    case ProvisioningWifiState::Failed:
      return "failed";
  }
  return "unknown";
}

const char *provisioning_session_error_name(ProvisioningSessionError error) {
  switch (error) {
    case ProvisioningSessionError::None:
      return "";
    case ProvisioningSessionError::BootstrapUnsupported:
      return "bootstrap_unsupported";
    case ProvisioningSessionError::BootstrapSessionExpired:
      return "bootstrap_session_expired";
    case ProvisioningSessionError::BootstrapConfirmationRequired:
      return "bootstrap_confirmation_required";
    case ProvisioningSessionError::BootstrapConfirmationFailed:
      return "bootstrap_confirmation_failed";
    case ProvisioningSessionError::WifiCredentialsInvalid:
      return "wifi_credentials_invalid";
    case ProvisioningSessionError::WifiConnectionTimeout:
      return "wifi_connection_timeout";
    case ProvisioningSessionError::WifiAuthFailed:
      return "wifi_auth_failed";
    case ProvisioningSessionError::DeviceIdentityMismatch:
      return "device_identity_mismatch";
    case ProvisioningSessionError::DeviceAlreadyBound:
      return "device_already_bound";
    case ProvisioningSessionError::DeviceRetired:
      return "device_retired";
    case ProvisioningSessionError::PlatformEnrollmentFailed:
      return "platform_enrollment_failed";
    case ProvisioningSessionError::PairingExpired:
      return "pairing_expired";
    case ProvisioningSessionError::TransportDisconnected:
      return "transport_disconnected";
    case ProvisioningSessionError::InvalidTransition:
      return "provisioning_session_invalid_transition";
  }
  return "unknown";
}

bool provisioning_session_phase_terminal(ProvisioningSessionPhase phase) {
  return phase == ProvisioningSessionPhase::LanControl ||
         phase == ProvisioningSessionPhase::Failed ||
         phase == ProvisioningSessionPhase::Expired;
}

}  // namespace lunaredu::lunaros

