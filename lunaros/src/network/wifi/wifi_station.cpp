#include "network/wifi/wifi_manager.h"

#include <mutex>
#include <esp_err.h>
#include <esp_wifi.h>

#include "lunaredu_lunaros/kernel/log.h"
#include "wifi_manager_internal.h"

namespace {

WifiDriverState &state = wifi_driver_state();
constexpr const char *kTag = "wifi_station";

} // namespace

bool wifi_connected() {
  std::lock_guard<std::recursive_mutex> lock(state.mutex);
  return wifi_driver_connected_locked();
}

bool wifi_queue_credentials_apply(const DeviceWifiCredential *credentials,
                                  size_t credential_count,
                                  uint8_t preferred_index) {
  return queue_wifi_credentials_apply(credentials, credential_count,
                                      preferred_index);
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

  LOGW(kTag, "主动调用 esp_wifi_disconnect()：原因=wifi_disconnect_station");
  const esp_err_t error = esp_wifi_disconnect();
  if (error != ESP_OK && error != ESP_ERR_WIFI_NOT_CONNECT) {
    LOGW(kTag, "主动断开 STA 连接失败：%s", esp_err_to_name(error));
  }
}

WifiRuntimeSnapshot wifi_runtime_snapshot() {
  std::lock_guard<std::recursive_mutex> lock(state.mutex);
  return {
      .connected = wifi_driver_connected_locked(),
      .credentials_available = state.credential_count > 0,
      .apply_pending = state.pending_apply,
      .station_connected = state.station_connected,
      .has_connected_once = state.has_connected_once,
      .connection_started_at_ms = state.first_connect_ms,
      .last_reconnect_ms = state.last_reconnect_ms,
      .last_status_log_ms = state.last_status_log_ms,
      .reconnect_attempts = state.reconnect_attempts,
      .disconnect_started_at_ms = state.disconnect_started_ms,
      .next_reconnect_delay_ms =
          wifi_driver_reconnect_delay(state.reconnect_attempts),
      .status_code = state.status,
      .last_disconnect_reason = state.last_disconnect_reason,
      .rssi = state.last_rssi,
      .station_suspended = state.station_suspended || !state.station_enabled,
      .softap_active = state.softap_active,
      .local_ip = state.local_ip,
      .target_ssid = state.ssid,
      .softap_ip = state.softap_ip,
  };
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
