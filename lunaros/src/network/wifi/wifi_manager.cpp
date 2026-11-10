#include "network/wifi/wifi_manager.h"

#include <mutex>

#include <esp_err.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_wifi.h>

#include "lunaredu_lunaros/kernel/log.h"
#include "network/wifi/wifi_manager_internal.h"
#include "network/wifi/wifi_reconnect_policy.h"
#include "network/wifi/wifi_reason.h"

namespace {

constexpr const char *kWifiLogTag = "wifi_manager";
constexpr uint32_t kStatusLogIntervalMs = 10000;
constexpr uint32_t kRssiSampleIntervalMs = 2000;

} // namespace

bool setup_wifi(bool station_enabled, bool keep_sta_radio, uint8_t radio_channel) {
  WifiDriverState &state = wifi_driver_state();
  {
    std::lock_guard<std::recursive_mutex> lock(state.mutex);
    state.station_enabled = station_enabled;
    state.station_suspended = !station_enabled;
    state.keep_sta_radio = keep_sta_radio;
    state.radio_channel = radio_channel;
    load_wifi_credentials_from_runtime();
  }
  if (!wifi_driver_ensure_stack_ready()) {
    return false;
  }

  LOGI(kWifiLogTag, "主动调用 esp_wifi_set_mode(STA)：原因=setup_wifi");
  const esp_err_t mode_error = esp_wifi_set_mode(WIFI_MODE_STA);
  if (mode_error != ESP_OK) {
    LOGE(kWifiLogTag, "设置 STA 模式失败：%s", esp_err_to_name(mode_error));
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
      LOGE(kWifiLogTag, "启动 Wi-Fi 失败：%s",
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
      LOGE(kWifiLogTag, "initial radio channel failed: %s", error.c_str());
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

  WifiRuntimeSnapshot snapshot = wifi_runtime_snapshot();
  uint32_t station_connected_since_ms = 0;
  {
    std::lock_guard<std::recursive_mutex> lock(state.mutex);
    station_connected_since_ms = state.station_connected_since_ms;
  }
  const WifiReconnectDecision reconnect = decide_wifi_reconnect({
      .now_ms = now,
      .last_reconnect_ms = snapshot.last_reconnect_ms,
      .reconnect_attempts = snapshot.reconnect_attempts,
      .station_connected_since_ms = station_connected_since_ms,
      .connected = connected,
      .station_suspended = snapshot.station_suspended,
      .credentials_available = snapshot.credentials_available,
      .station_connected = snapshot.station_connected,
      .connection_in_progress =
          snapshot.status_code == kWifiStatusIdle &&
          !snapshot.station_connected,
      .has_connected_once = snapshot.has_connected_once,
  });
  if (reconnect.should_reconnect) {
    uint32_t disconnect_elapsed_ms = 0;
    {
      std::lock_guard<std::recursive_mutex> lock(state.mutex);
      const uint32_t started_at =
          wifi_driver_ensure_reconnect_window_started_locked(now);
      disconnect_elapsed_ms = now - started_at;
    }
    if (reconnect.waiting_for_dhcp) {
      LOGW(kWifiLogTag,
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

  snapshot = wifi_runtime_snapshot();
  if (now - snapshot.last_status_log_ms >= kStatusLogIntervalMs) {
    wifi_driver_log_status();
    std::lock_guard<std::recursive_mutex> lock(state.mutex);
    state.last_status_log_ms = now;
  }
}
