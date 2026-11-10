#include "network/wifi/wifi_reconnect_policy.h"

namespace {

constexpr uint32_t kReconnectFirstRetryMs = 1000;
constexpr uint32_t kReconnectSecondRetryMs = 2000;
constexpr uint32_t kReconnectThirdRetryMs = 4000;
constexpr uint32_t kReconnectFourthRetryMs = 8000;
constexpr uint32_t kReconnectSteadyRetryMs = 15000;
constexpr uint32_t kReconnectLongRetryMs = 30000;

} // namespace

uint32_t wifi_reconnect_delay_for_attempts(uint32_t attempts) {
  switch (attempts) {
  case 0:
    return kReconnectFirstRetryMs;
  case 1:
    return kReconnectSecondRetryMs;
  case 2:
    return kReconnectThirdRetryMs;
  case 3:
    return kReconnectFourthRetryMs;
  default:
    return attempts < 8 ? kReconnectSteadyRetryMs : kReconnectLongRetryMs;
  }
}

WifiReconnectDecision
decide_wifi_reconnect(const WifiReconnectInput &input) {
  WifiReconnectDecision decision;
  decision.reconnect_delay_ms =
      wifi_reconnect_delay_for_attempts(input.reconnect_attempts);
  decision.waiting_for_dhcp = input.station_connected && !input.connected;
  const uint32_t station_connected_elapsed_ms =
      input.now_ms - input.station_connected_since_ms;
  const bool station_connected_timestamp_reached =
      static_cast<int32_t>(station_connected_elapsed_ms) >= 0;
  if (decision.waiting_for_dhcp && station_connected_timestamp_reached) {
    decision.station_connected_elapsed_ms = station_connected_elapsed_ms;
  }
  decision.dhcp_wait_expired =
      !decision.waiting_for_dhcp ||
      (station_connected_timestamp_reached &&
       decision.station_connected_elapsed_ms >= kWifiDhcpLeaseWaitMs);

  // A configuration apply can start a connection after tick_wifi() captured
  // its timestamp. Treat that newer reconnect timestamp as "not elapsed yet"
  // instead of allowing unsigned subtraction to look like a multi-week delay.
  // Casting the modular delta preserves the legitimate uint32 clock-wrap case
  // as long as intervals stay below INT32_MAX milliseconds.
  const uint32_t reconnect_elapsed_ms =
      input.now_ms - input.last_reconnect_ms;
  const bool reconnect_timestamp_reached =
      static_cast<int32_t>(reconnect_elapsed_ms) >= 0;

  decision.should_reconnect =
      !input.connected && !input.station_suspended &&
      input.credentials_available &&
      !input.connection_in_progress &&
      reconnect_timestamp_reached &&
      reconnect_elapsed_ms >= decision.reconnect_delay_ms &&
      decision.dhcp_wait_expired;
  decision.should_advance_credential =
      decision.should_reconnect && !input.station_connected;
  return decision;
}
