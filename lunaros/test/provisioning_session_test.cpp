#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>

#include "lunaredu_lunaros/network/provisioning_session.h"

using namespace lunaredu::lunaros;

namespace {

ProvisioningSessionEvent event(ProvisioningSessionEventType type) {
  ProvisioningSessionEvent result;
  result.type = type;
  return result;
}

void test_state_machine_reaches_lan_control() {
  ProvisioningSessionSnapshot snapshot = create_provisioning_session(10'000);
  std::string error;

  ProvisioningSessionEvent open = event(ProvisioningSessionEventType::OpenSession);
  open.session_expires_at_ms = 5'000;
  assert(transition_provisioning_session(snapshot, open, error));
  assert(snapshot.phase == ProvisioningSessionPhase::LocalSessionAuthenticated);

  assert(transition_provisioning_session(
      snapshot, event(ProvisioningSessionEventType::Confirmed), error));
  assert(transition_provisioning_session(
      snapshot, event(ProvisioningSessionEventType::WifiSubmitted), error));
  ProvisioningSessionEvent connecting =
      event(ProvisioningSessionEventType::ProvisioningStatus);
  connecting.wifi_state = ProvisioningWifiState::Connecting;
  assert(transition_provisioning_session(snapshot, connecting, error));

  ProvisioningSessionEvent connected =
      event(ProvisioningSessionEventType::ProvisioningStatus);
  connected.wifi_state = ProvisioningWifiState::Connected;
  assert(transition_provisioning_session(snapshot, connected, error));
  assert(transition_provisioning_session(
      snapshot, event(ProvisioningSessionEventType::PlatformEnrolled), error));
  assert(transition_provisioning_session(
      snapshot, event(ProvisioningSessionEventType::Bound), error));
  assert(transition_provisioning_session(
      snapshot, event(ProvisioningSessionEventType::LanControl), error));
  assert(snapshot.phase == ProvisioningSessionPhase::LanControl);
  assert(snapshot.wifi_state == ProvisioningWifiState::Connected);
  assert(error.empty());
}

void test_invalid_order_does_not_mutate_state() {
  ProvisioningSessionSnapshot snapshot = create_provisioning_session(10'000);
  std::string error;
  assert(!transition_provisioning_session(
      snapshot, event(ProvisioningSessionEventType::WifiSubmitted), error));
  assert(snapshot.phase == ProvisioningSessionPhase::Discovered);
  assert(error == "provisioning_session_invalid_transition");
}

void test_expiry_handles_wraparound_and_preserves_identity_state() {
  ProvisioningSessionSnapshot snapshot = create_provisioning_session(100);
  snapshot.phase = ProvisioningSessionPhase::WifiProvisioning;
  snapshot.wifi_state = ProvisioningWifiState::Unconfigured;
  assert(!provisioning_session_expired(snapshot, 99));
  assert(provisioning_session_expired(snapshot, 100));
  assert(expire_provisioning_session(snapshot, 100));
  assert(snapshot.phase == ProvisioningSessionPhase::Expired);
  assert(snapshot.error == ProvisioningSessionError::BootstrapSessionExpired);
  assert(snapshot.wifi_state == ProvisioningWifiState::Unconfigured);
  assert(snapshot.session_expires_at_ms == 0);
}

void test_wire_names_and_terminal_boundaries() {
  assert(std::string(provisioning_session_phase_name(
             ProvisioningSessionPhase::LocalSessionAuthenticated)) ==
         "local_session_authenticated");
  assert(std::string(provisioning_wifi_state_name(
             ProvisioningWifiState::Connected)) == "connected");
  assert(std::string(provisioning_session_error_name(
             ProvisioningSessionError::WifiAuthFailed)) == "wifi_auth_failed");
  assert(!provisioning_session_phase_terminal(ProvisioningSessionPhase::Bound));
  assert(provisioning_session_phase_terminal(ProvisioningSessionPhase::LanControl));
}

}  // namespace

int main() {
  test_state_machine_reaches_lan_control();
  test_invalid_order_does_not_mutate_state();
  test_expiry_handles_wraparound_and_preserves_identity_state();
  test_wire_names_and_terminal_boundaries();
  std::cout << "provisioning session tests passed: 4\n";
  return 0;
}

