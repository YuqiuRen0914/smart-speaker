#include <cassert>
#include <cstdint>
#include <iostream>
#include <limits>

#include "network/wifi/wifi_reconnect_policy.h"

namespace {

WifiReconnectInput reconnectable_input() {
  return {
      .now_ms = 10000,
      .last_reconnect_ms = 0,
      .reconnect_attempts = 0,
      .station_connected_since_ms = 0,
      .connected = false,
      .station_suspended = false,
      .credentials_available = true,
      .station_connected = false,
      .connection_in_progress = false,
      .has_connected_once = false,
  };
}

void test_backoff_schedule() {
  assert(wifi_reconnect_delay_for_attempts(0) == 1000);
  assert(wifi_reconnect_delay_for_attempts(1) == 2000);
  assert(wifi_reconnect_delay_for_attempts(2) == 4000);
  assert(wifi_reconnect_delay_for_attempts(3) == 8000);
  assert(wifi_reconnect_delay_for_attempts(4) == 15000);
  assert(wifi_reconnect_delay_for_attempts(7) == 15000);
  assert(wifi_reconnect_delay_for_attempts(8) == 30000);
}

void test_reconnect_requires_available_unsuspended_station() {
  WifiReconnectInput input = reconnectable_input();
  assert(decide_wifi_reconnect(input).should_reconnect);

  input.connected = true;
  assert(!decide_wifi_reconnect(input).should_reconnect);
  input.connected = false;
  input.station_suspended = true;
  assert(!decide_wifi_reconnect(input).should_reconnect);
  input.station_suspended = false;
  input.credentials_available = false;
  assert(!decide_wifi_reconnect(input).should_reconnect);
}

void test_backoff_time_must_expire() {
  WifiReconnectInput input = reconnectable_input();
  input.now_ms = 1999;
  input.last_reconnect_ms = 1000;
  assert(!decide_wifi_reconnect(input).should_reconnect);
  input.now_ms = 2000;
  assert(decide_wifi_reconnect(input).should_reconnect);
}

void test_active_connection_attempt_is_not_interrupted() {
  WifiReconnectInput input = reconnectable_input();
  input.connection_in_progress = true;
  assert(!decide_wifi_reconnect(input).should_reconnect);
  assert(!decide_wifi_reconnect(input).should_advance_credential);
}

void test_dhcp_wait_prevents_early_reconnect() {
  WifiReconnectInput input = reconnectable_input();
  input.now_ms = 20000;
  input.station_connected = true;
  input.station_connected_since_ms = 6000;
  WifiReconnectDecision decision = decide_wifi_reconnect(input);
  assert(decision.waiting_for_dhcp);
  assert(!decision.dhcp_wait_expired);
  assert(!decision.should_reconnect);

  input.now_ms = 21000;
  decision = decide_wifi_reconnect(input);
  assert(decision.dhcp_wait_expired);
  assert(decision.should_reconnect);
  assert(!decision.should_advance_credential);
}

void test_credential_advances_only_before_first_success() {
  WifiReconnectInput input = reconnectable_input();
  assert(decide_wifi_reconnect(input).should_advance_credential);
  input.has_connected_once = true;
  assert(!decide_wifi_reconnect(input).should_advance_credential);
  input.has_connected_once = false;
  input.station_connected = true;
  input.station_connected_since_ms = input.now_ms - kWifiDhcpLeaseWaitMs;
  assert(!decide_wifi_reconnect(input).should_advance_credential);
}

void test_elapsed_time_handles_clock_wraparound() {
  WifiReconnectInput input = reconnectable_input();
  input.last_reconnect_ms = std::numeric_limits<uint32_t>::max() - 499;
  input.now_ms = 500;
  assert(decide_wifi_reconnect(input).should_reconnect);
}

void test_newer_reconnect_timestamp_does_not_underflow() {
  WifiReconnectInput input = reconnectable_input();
  input.now_ms = 1000;
  input.last_reconnect_ms = 1006;
  assert(!decide_wifi_reconnect(input).should_reconnect);
}

void test_newer_station_timestamp_does_not_fake_dhcp_timeout() {
  WifiReconnectInput input = reconnectable_input();
  input.now_ms = 1000;
  input.station_connected = true;
  input.station_connected_since_ms = 1001;
  const WifiReconnectDecision decision = decide_wifi_reconnect(input);
  assert(decision.waiting_for_dhcp);
  assert(decision.station_connected_elapsed_ms == 0);
  assert(!decision.dhcp_wait_expired);
  assert(!decision.should_reconnect);
}

} // namespace

int main() {
  test_backoff_schedule();
  test_reconnect_requires_available_unsuspended_station();
  test_backoff_time_must_expire();
  test_active_connection_attempt_is_not_interrupted();
  test_dhcp_wait_prevents_early_reconnect();
  test_credential_advances_only_before_first_success();
  test_elapsed_time_handles_clock_wraparound();
  test_newer_reconnect_timestamp_does_not_underflow();
  test_newer_station_timestamp_does_not_fake_dhcp_timeout();
  std::cout << "wifi reconnect policy tests passed: 9\n";
  return 0;
}
