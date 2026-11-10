#include "network/network_state_policy.h"

namespace lunaredu::lunaros::internal {
namespace {

constexpr uint32_t kDefaultProvisioningFailureTimeoutMs = 180000;
constexpr uint32_t kDefaultProvisioningFailureAttempts = 10;

uint32_t elapsed_since_ms(uint32_t now_ms, uint32_t started_at_ms) {
  return started_at_ms == 0 ? 0 : now_ms - started_at_ms;
}

} // namespace

NetworkProvisioningDecision decide_network_provisioning(
    const NetworkProvisioningPolicy &policy,
    const NetworkObservation &observation, bool control_supported) {
  NetworkProvisioningDecision decision;
  if (!policy.enabled || !control_supported) {
    return decision;
  }

  const bool provisioning_active =
      observation.provisioning != NetworkProvisioningState::Idle;
  if (policy.stop_when_connected && provisioning_active &&
      ((observation.link == NetworkLinkState::Up &&
        !observation.station_suspended) ||
       observation.provisioning == NetworkProvisioningState::Applying)) {
    decision.action = NetworkProvisioningAction::Stop;
    return decision;
  }

  if (provisioning_active || observation.link == NetworkLinkState::Up ||
      policy.mode == NetworkProvisioningMode::Disabled) {
    return decision;
  }

  bool should_start = !observation.credentials_available &&
                      policy.start_when_credentials_missing;
  if (!should_start && observation.credentials_available &&
      policy.start_when_connection_times_out) {
    const uint32_t timeout_ms = policy.reconnect_failure_timeout_ms > 0
                                    ? policy.reconnect_failure_timeout_ms
                                    : kDefaultProvisioningFailureTimeoutMs;
    const uint32_t minimum_attempts =
        policy.reconnect_failure_min_attempts > 0
            ? policy.reconnect_failure_min_attempts
            : kDefaultProvisioningFailureAttempts;
    should_start =
        elapsed_since_ms(observation.monotonic_ms,
                         observation.connection_failure_started_at_ms) >=
            timeout_ms &&
        observation.reconnect_attempts >= minimum_attempts;
  }

  if (should_start) {
    decision.action = NetworkProvisioningAction::Start;
    decision.mode = policy.mode;
    decision.suspend_station = policy.suspend_station_while_provisioning;
  }
  return decision;
}

bool apply_network_observation(NetworkSnapshot &snapshot,
                               const NetworkObservation &observation) {
  const bool platform_changed =
      snapshot.platform_ready != observation.ready ||
      snapshot.credentials_available != observation.credentials_available ||
      snapshot.link != observation.link ||
      snapshot.rssi != observation.rssi ||
      snapshot.provisioning != observation.provisioning ||
      snapshot.clock != observation.clock ||
      snapshot.local_ip != observation.local_ip ||
      snapshot.target_ssid != observation.target_ssid ||
      snapshot.provisioning_ssid != observation.provisioning_ssid ||
      snapshot.provisioning_ip != observation.provisioning_ip;
  const bool reset_internet =
      observation.link != NetworkLinkState::Up &&
      snapshot.internet != NetworkInternetState::Unknown;
  if (!platform_changed && !reset_internet) {
    return false;
  }

  if (platform_changed) {
    snapshot.platform_ready = observation.ready;
    snapshot.credentials_available = observation.credentials_available;
    snapshot.link = observation.link;
    snapshot.rssi = observation.rssi;
    snapshot.provisioning = observation.provisioning;
    snapshot.clock = observation.clock;
    snapshot.local_ip = observation.local_ip;
    snapshot.target_ssid = observation.target_ssid;
    snapshot.provisioning_ssid = observation.provisioning_ssid;
    snapshot.provisioning_ip = observation.provisioning_ip;
  }
  if (reset_internet) {
    snapshot.internet = NetworkInternetState::Unknown;
  }
  return true;
}

} // namespace lunaredu::lunaros::internal
