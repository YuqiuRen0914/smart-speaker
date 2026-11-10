#include "network/wifi/wifi_manager.h"

#include <mutex>

#include <esp_err.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_wifi.h>

#include <esp_log.h>
#include "network/wifi/wifi_manager_internal.h"
#include "network/wifi/wifi_reconnect_policy.h"
#include "network/wifi/wifi_reason.h"

namespace {

constexpr const char *kWifiLogTag = "wifi_manager";
constexpr uint32_t kStatusLogIntervalMs = 10000;
constexpr uint32_t kRssiSampleIntervalMs = 2000;

} // namespace

bool setup_wifi(bool station_enabled, bool keep_sta_radio, uint8_t radio_channel,
                bool default_fallback_enabled) {
  WifiDriverState &state = wifi_driver_state();
  {
    std::lock_guard<std::recursive_mutex> lock(state.mutex);
    state.station_enabled = station_enabled;
    state.default_fallback_enabled = default_fallback_enabled;
    state.station_suspended = !station_enabled;
    state.keep_sta_radio = keep_sta_radio;
    state.radio_channel = radio_channel;
    load_wifi_credentials_from_runtime();
  }
  if (!wifi_driver_ensure_stack_ready()) {
    return false;
  }

  ESP_LOGI(kWifiLogTag, "主动调用 esp_wifi_set_mode(STA)：原因=setup_wifi");
  const esp_err_t mode_error = esp_wifi_set_mode(WIFI_MODE_STA);
  if (mode_error != ESP_OK) {
    ESP_LOGE(kWifiLogTag, "设置 STA 模式失败：%s", esp_err_to_name(mode_error));
    return false;
  }
  bool should_start = false;
  {
    std::lock_guard<std::recursive_mutex> lock(state.mutex);
    should_start = !state.sta_started;
  }
  if (should_start) {
    const esp_err_t start_error = esp_wifi_start();
    if (start_error != ESP_OK) {
      ESP_LOGE(kWifiLogTag, "启动 Wi-Fi 失败：%s",
           esp_err_to_name(start_error));
      return false;
    }
    std::lock_guard<std::recursive_mutex> lock(state.mutex);
    state.sta_started = true;
  }

  wifi_driver_apply_power_save_setting();

  bool has_credentials = false;
  {
    std::lock_guard<std::recursive_mutex> lock(state.mutex);
    state.first_connect_ms =
        static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
    has_credentials = state.credential_count > 0 && !state.station_suspended;
    state.station_connected = false;
    state.station_connected_since_ms = 0;
    state.has_connected_once = false;
    wifi_driver_reset_reconnect_window_locked();
  }
  if (!has_credentials && keep_sta_radio) {
    std::string error;
    if (!wifi_set_radio_channel(radio_channel, error)) {
      ESP_LOGE(kWifiLogTag, "initial radio channel failed: %s", error.c_str());
      return false;
    }
  }
  if (has_credentials) {
    wifi_driver_begin_connection();
  }
  return true;
}

bool stop_wifi(std::string &error) {
  return wifi_driver_shutdown_stack(error);
}

void tick_wifi() {
  // Keep credential rotation and reconnect submission in the same radio lease.
  // A scan may take seconds; defer this tick instead of blocking the owner.
  std::unique_lock<std::recursive_mutex> radio_lock(wifi_radio_mutex(), std::try_to_lock);
  if (!radio_lock.owns_lock()) return;
  WifiDriverState &state = wifi_driver_state();
  const uint32_t now =
      static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);

  wifi_driver_drain_events();
  wifi_driver_apply_pending_config();

  {
    std::lock_guard<std::recursive_mutex> lock(state.mutex);
    if(wifi_driver_connected_locked()&&state.was_connected&&
       state.last_rssi_sample_ms&&now-state.last_rssi_sample_ms<kRssiSampleIntervalMs&&
       now-state.last_status_log_ms<kStatusLogIntervalMs)return;
  }
  const bool connected = wifi_connected();
  bool should_update_rssi = false;
  if (connected) {
    std::lock_guard<std::recursive_mutex> lock(state.mutex);
    should_update_rssi = state.last_rssi_sample_ms == 0 ||
        now - state.last_rssi_sample_ms >= kRssiSampleIntervalMs;
  }
  if (should_update_rssi) {
    wifi_driver_update_rssi_snapshot();
  }
  if (connected) {
    bool should_handle_connected = false;
    {
      std::lock_guard<std::recursive_mutex> lock(state.mutex);
      should_handle_connected = !state.was_connected;
    }
    if (should_handle_connected) {
      handle_wifi_connected_once();
    }
  } else {
    std::lock_guard<std::recursive_mutex> lock(state.mutex);
    if (state.was_connected) {
      reset_wifi_attempt_sequence(state.preferred_credential_index);
    }
    state.was_connected = false;
  }

  // Reconnect policy needs only scalar facts. Copy the SSID only on the rare
  // reconnect path where it is actually logged, not on every owner tick.
  WifiReconnectDecision reconnect;
  uint32_t last_status_log_ms;
  uint32_t station_connected_since_ms = 0;
  {
    std::lock_guard<std::recursive_mutex> lock(state.mutex);
    station_connected_since_ms = state.station_connected_since_ms;
    last_status_log_ms = state.last_status_log_ms;
    reconnect = decide_wifi_reconnect({
      .now_ms = now,
      .last_reconnect_ms = state.last_reconnect_ms,
      .reconnect_attempts = state.reconnect_attempts,
      .station_connected_since_ms = station_connected_since_ms,
      .connected = connected,
      .station_suspended = state.station_suspended || !state.station_enabled ||
          state.pending_apply || state.pending_disconnect || !state.stack_ready,
      .credentials_available = state.credential_count > 0,
      .station_connected = state.station_connected,
      .connection_in_progress =
          state.status == kWifiStatusIdle && !state.station_connected,
      .has_connected_once = state.has_connected_once,
  });
  }
  if (reconnect.should_reconnect) {
    const auto snapshot = wifi_runtime_snapshot();
    uint32_t disconnect_elapsed_ms = 0;
    {
      std::lock_guard<std::recursive_mutex> lock(state.mutex);
      const uint32_t started_at =
          wifi_driver_ensure_reconnect_window_started_locked(now);
      disconnect_elapsed_ms = now - started_at;
    }
    if (reconnect.waiting_for_dhcp) {
      ESP_LOGW(kWifiLogTag,
           "DHCP 等待超时，重新连接：associated=%u ms timeout=%u ms SSID=%s",
           static_cast<unsigned>(reconnect.station_connected_elapsed_ms),
           static_cast<unsigned>(kWifiDhcpLeaseWaitMs),
           snapshot.target_ssid.c_str());
    } else {
      ESP_LOGI(kWifiLogTag,
               "准备重连 Wi-Fi：attempt=%u delay=%u ms elapsed=%u ms "
               "reason=%d/%s state=%d",
               static_cast<unsigned>(snapshot.reconnect_attempts + 1),
               static_cast<unsigned>(reconnect.reconnect_delay_ms),
               static_cast<unsigned>(disconnect_elapsed_ms),
               snapshot.last_disconnect_reason,
               wifi_disconnect_reason_name(snapshot.last_disconnect_reason),
               snapshot.status_code);
    }
    if (reconnect.should_advance_credential) {
      std::lock_guard<std::recursive_mutex> lock(state.mutex);
      advance_wifi_candidate();
    }
    wifi_driver_begin_connection();
  }

  if (now - last_status_log_ms >= kStatusLogIntervalMs) {
    wifi_driver_log_status();
    std::lock_guard<std::recursive_mutex> lock(state.mutex);
    state.last_status_log_ms = now;
  }
}


WifiDriverState &wifi_driver_state() {
  // Static storage keeps the ESP32 Wi-Fi backend allocation-free and does not create
  // an additional FreeRTOS task. Construction is completed before first use.
  static WifiDriverState state;
  return state;
}

uint32_t wifi_driver_reconnect_delay(uint32_t attempts) {
  return wifi_reconnect_delay_for_attempts(attempts);
}

bool wifi_driver_connected_locked() {
  return wifi_driver_state().status == kWifiStatusConnected;
}
