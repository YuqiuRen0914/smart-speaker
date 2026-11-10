#include "lunaros_network.h"
#include "lunaros_network_policy.h"
#include "network/wifi/wifi_manager.h"

#include <mutex>
#include <esp_err.h>
#include <esp_wifi.h>

#include <esp_log.h>
#include "wifi_manager_internal.h"

namespace {

WifiDriverState &state = wifi_driver_state();
constexpr const char *kTag = "wifi_station";

template <typename Snapshot>
void copy_wifi_facts_locked(Snapshot &output, bool connected) {
  output.wifi_events = state.wifi_events;
  output.credentials_available = state.credential_count > 0;
  output.reconnect_attempts = state.reconnect_attempts;
  output.disconnect_started_at_ms = state.disconnect_started_ms;
  output.next_reconnect_delay_ms = wifi_driver_reconnect_delay(state.reconnect_attempts);
  output.status_code = state.status;
  output.last_disconnect_reason = state.last_disconnect_reason;
  output.rssi = state.last_rssi;
  output.channel = connected ? state.last_station_channel : 0;
  output.station_suspended = state.station_suspended || !state.station_enabled;
  output.softap_active = state.softap_active;
  if (output.local_ip != state.local_ip) output.local_ip = state.local_ip;
  if (output.target_ssid != state.ssid) output.target_ssid = state.ssid;
}

} // namespace

bool wifi_connected() {
  std::lock_guard<std::recursive_mutex> lock(state.mutex);
  return wifi_driver_connected_locked();
}

bool wifi_queue_credentials_apply(const DeviceWifiCredential *credentials,
                                  size_t credential_count,
                                  uint8_t preferred_index, bool temporary) {
  return queue_wifi_credentials_apply(credentials, credential_count,
                                      preferred_index,temporary);
}

void wifi_set_station_suspended(bool suspended) {
  std::lock_guard<std::recursive_mutex> lock(state.mutex);
  state.station_suspended = suspended || !state.station_enabled;
  if (suspended) {
    // esp_wifi_disconnect() reports completion asynchronously. Publish the
    // deliberate suspension immediately so the provisioning policy cannot
    // mistake a stale connected snapshot for a reason to stop SoftAP again.
    state.status = kWifiStatusDisconnected;
    state.station_connected = false;
    state.station_connected_since_ms = 0;
    state.local_ip.clear();
    state.last_rssi = 0;
  }
}

bool wifi_station_suspended() {
  std::lock_guard<std::recursive_mutex> lock(state.mutex);
  return state.station_suspended;
}

void wifi_disconnect_station() {
  {
    std::lock_guard<std::recursive_mutex> lock(state.mutex);
    if (!state.stack_ready) {
      return;
    }
  }

  ESP_LOGW(kTag, "主动调用 esp_wifi_disconnect()：原因=wifi_disconnect_station");
  const esp_err_t error = esp_wifi_disconnect();
  if (error != ESP_OK && error != ESP_ERR_WIFI_NOT_CONNECT) {
    ESP_LOGW(kTag, "主动断开 STA 连接失败：%s", esp_err_to_name(error));
  }
}

WifiRuntimeSnapshot wifi_runtime_snapshot() {
  WifiRuntimeSnapshot output;
  wifi_runtime_snapshot_into(output);
  return output;
}
void wifi_runtime_snapshot_into(WifiRuntimeSnapshot &output) {
  std::lock_guard<std::recursive_mutex> lock(state.mutex);
  output.configuration_generation=state.configuration_generation;
  output.connected = wifi_driver_connected_locked();
  output.apply_pending = state.pending_apply;
  copy_wifi_facts_locked(output, output.connected);
}

void wifi_observe_network(lunaredu::lunaros::NetworkPhysicalState &output,
                          bool local_access_active) {
  using lunaredu::lunaros::NetworkLinkState;
  std::lock_guard<std::recursive_mutex> lock(state.mutex);
  const bool connected = wifi_driver_connected_locked();
  copy_wifi_facts_locked(output, connected);
  output.link = connected ? NetworkLinkState::Up
      : (!output.station_suspended && !output.target_ssid.empty() && !local_access_active
          ? NetworkLinkState::Connecting : NetworkLinkState::Down);
  if (output.local_access_ip != state.softap_ip) output.local_access_ip = state.softap_ip;
}

int32_t wifi_rssi() {
  wifi_driver_update_rssi_snapshot();
  std::lock_guard<std::recursive_mutex> lock(state.mutex);
  return state.last_rssi;
}


bool wifi_set_power_save_level(NetworkWifiPowerSaveLevel level) {
  if (!wifi_driver_ensure_stack_ready()) {
    return false;
  }

  wifi_ps_type_t ps_type = WIFI_PS_MIN_MODEM;
  switch (level) {
  case NetworkWifiPowerSaveLevel::LOW_POWER:
    ps_type = WIFI_PS_MAX_MODEM;
    break;
  case NetworkWifiPowerSaveLevel::BALANCED:
    ps_type = WIFI_PS_MIN_MODEM;
    break;
  case NetworkWifiPowerSaveLevel::PERFORMANCE:
    ps_type = WIFI_PS_NONE;
    break;
  }

  {
    std::lock_guard<std::recursive_mutex> lock(state.mutex);
    state.power_save_type = ps_type;
  }
  return wifi_driver_apply_power_save_setting();
}
