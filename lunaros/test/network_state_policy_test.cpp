#include <cassert>
#include <iostream>

#include "network/network_state_policy.h"

using namespace lunaredu::lunaros;

namespace {

void test_missing_credentials_starts_provisioning() {
  NetworkProvisioningPolicy policy;
  NetworkObservation observation;
  const auto decision =
      internal::decide_network_provisioning(policy, observation, true);
  assert(decision.action == internal::NetworkProvisioningAction::Start);
  assert(decision.mode == NetworkProvisioningMode::SoftAp);
  assert(decision.suspend_station);
}

void test_connection_failure_respects_time_and_attempt_thresholds() {
  NetworkProvisioningPolicy policy;
  policy.reconnect_failure_timeout_ms = 5000;
  policy.reconnect_failure_min_attempts = 3;
  NetworkObservation observation;
  observation.credentials_available = true;
  observation.connection_failure_started_at_ms = 1000;
  observation.monotonic_ms = 5999;
  observation.reconnect_attempts = 3;
  assert(internal::decide_network_provisioning(policy, observation, true)
             .action == internal::NetworkProvisioningAction::None);
  observation.monotonic_ms = 6000;
  assert(internal::decide_network_provisioning(policy, observation, true)
             .action == internal::NetworkProvisioningAction::Start);
}

void test_default_connection_failure_threshold_delays_softap() {
  NetworkProvisioningPolicy policy;
  NetworkObservation observation;
  observation.credentials_available = true;
  observation.connection_failure_started_at_ms = 1000;
  observation.monotonic_ms = 180999;
  observation.reconnect_attempts = 10;
  assert(internal::decide_network_provisioning(policy, observation, true)
             .action == internal::NetworkProvisioningAction::None);

  observation.monotonic_ms = 181000;
  observation.reconnect_attempts = 9;
  assert(internal::decide_network_provisioning(policy, observation, true)
             .action == internal::NetworkProvisioningAction::None);

  observation.reconnect_attempts = 10;
  assert(internal::decide_network_provisioning(policy, observation, true)
             .action == internal::NetworkProvisioningAction::Start);
}

void test_active_provisioning_stops_after_link_or_apply() {
  NetworkProvisioningPolicy policy;
  NetworkObservation observation;
  observation.provisioning = NetworkProvisioningState::Active;
  observation.link = NetworkLinkState::Up;
  assert(internal::decide_network_provisioning(policy, observation, true)
             .action == internal::NetworkProvisioningAction::Stop);
  observation.link = NetworkLinkState::Down;
  observation.provisioning = NetworkProvisioningState::Applying;
  assert(internal::decide_network_provisioning(policy, observation, true)
      .action == internal::NetworkProvisioningAction::Stop);
}

void test_suspended_station_does_not_stop_new_softap_on_stale_link() {
  NetworkProvisioningPolicy policy;
  NetworkObservation observation;
  observation.provisioning = NetworkProvisioningState::Active;
  observation.link = NetworkLinkState::Up;
  observation.station_suspended = true;
  assert(internal::decide_network_provisioning(policy, observation, true)
             .action == internal::NetworkProvisioningAction::None);

  observation.provisioning = NetworkProvisioningState::Applying;
  assert(internal::decide_network_provisioning(policy, observation, true)
             .action == internal::NetworkProvisioningAction::Stop);
}

void test_disabled_or_unsupported_control_is_inert() {
  NetworkProvisioningPolicy policy;
  NetworkObservation observation;
  assert(internal::decide_network_provisioning(policy, observation, false)
             .action == internal::NetworkProvisioningAction::None);
  policy.enabled = false;
  assert(internal::decide_network_provisioning(policy, observation, true)
             .action == internal::NetworkProvisioningAction::None);
}

void test_observation_updates_only_driver_fields() {
  NetworkSnapshot snapshot;
  snapshot.driver_bound = true;
  snapshot.service_started = true;
  snapshot.device_link = NetworkDeviceLinkState::Ready;
  snapshot.last_error = "retained";
  NetworkObservation observation;
  observation.ready = true;
  observation.credentials_available = true;
  observation.link = NetworkLinkState::Up;
  observation.rssi = -61;
  observation.local_ip = "192.168.1.8";
  assert(internal::apply_network_observation(snapshot, observation));
  assert(snapshot.platform_ready);
  assert(snapshot.local_ip == "192.168.1.8");
  assert(snapshot.rssi == -61);
  assert(snapshot.driver_bound && snapshot.service_started);
  assert(snapshot.device_link == NetworkDeviceLinkState::Ready);
  assert(snapshot.last_error == "retained");
  assert(!internal::apply_network_observation(snapshot, observation));
  observation.rssi = -72;
  assert(internal::apply_network_observation(snapshot, observation));
  assert(snapshot.rssi == -72);
}

void test_link_down_invalidates_internet_reachability() {
  NetworkSnapshot snapshot;
  snapshot.link = NetworkLinkState::Up;
  snapshot.internet = NetworkInternetState::Reachable;
  NetworkObservation observation;
  observation.link = NetworkLinkState::Down;
  assert(internal::apply_network_observation(snapshot, observation));
  assert(snapshot.internet == NetworkInternetState::Unknown);
}

} // namespace

int main() {
  test_missing_credentials_starts_provisioning();
  test_connection_failure_respects_time_and_attempt_thresholds();
  test_default_connection_failure_threshold_delays_softap();
  test_active_provisioning_stops_after_link_or_apply();
  test_suspended_station_does_not_stop_new_softap_on_stale_link();
  test_disabled_or_unsupported_control_is_inert();
  test_observation_updates_only_driver_fields();
  test_link_down_invalidates_internet_reachability();
  std::cout << "network state policy tests passed: 8\n";
  return 0;
}
