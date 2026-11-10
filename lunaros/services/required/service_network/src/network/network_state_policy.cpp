#include "lunaros_network.h"
#include "network/network_state_policy.h"

namespace lunaredu::lunaros::internal {
namespace {
bool same_physical_state(const NetworkPhysicalState &a, const NetworkPhysicalState &b) {
  return a.wifi_events == b.wifi_events && a.platform_ready == b.platform_ready &&
      a.credentials_available == b.credentials_available && a.link == b.link &&
      a.rssi == b.rssi && a.status_code == b.status_code &&
      a.last_disconnect_reason == b.last_disconnect_reason && a.channel == b.channel &&
      a.station_suspended == b.station_suspended && a.softap_active == b.softap_active &&
      a.bluetooth_available == b.bluetooth_available && a.bluetooth_enabled == b.bluetooth_enabled &&
      a.bluetooth_connected == b.bluetooth_connected && a.reconnect_attempts == b.reconnect_attempts &&
      a.disconnect_started_at_ms == b.disconnect_started_at_ms &&
      a.next_reconnect_delay_ms == b.next_reconnect_delay_ms &&
      a.local_access == b.local_access && a.clock == b.clock &&
      a.local_ip == b.local_ip && a.target_ssid == b.target_ssid &&
      a.local_access_ssid == b.local_access_ssid && a.local_access_ip == b.local_access_ip;
}
} // namespace
bool apply_network_observation(NetworkSnapshot &snapshot,
                               const NetworkObservation &observation) {
  auto &physical = static_cast<NetworkPhysicalState &>(snapshot);
  if (same_physical_state(physical, observation)) return false;
  physical = observation;
  return true;
}

} // namespace lunaredu::lunaros::internal
