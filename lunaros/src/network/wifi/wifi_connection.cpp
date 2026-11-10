#include "network/wifi/wifi_manager_internal.h"

#include <cstring>
#include <mutex>
#include <string>

#include <esp_err.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_wifi.h>

#include "lunaredu_lunaros/kernel/log.h"
#include "network/wifi/wifi_reason.h"

namespace {

constexpr const char *kWifiLogTag = "wifi_connection";

} // namespace

void wifi_driver_reset_reconnect_window_locked() {
  WifiDriverState &state = wifi_driver_state();
  state.disconnect_started_ms = 0;
  state.reconnect_attempts = 0;
}

uint32_t wifi_driver_ensure_reconnect_window_started_locked(uint32_t now_ms) {
  WifiDriverState &state = wifi_driver_state();
  if (state.disconnect_started_ms == 0) {
    state.disconnect_started_ms = now_ms;
  }
  return state.disconnect_started_ms;
}

void wifi_driver_begin_connection() {
  // Never wait for a background scan on the Network owner. tick_wifi retries.
  std::unique_lock<std::recursive_mutex> radio_lock(wifi_radio_mutex(), std::try_to_lock);
  if (!radio_lock.owns_lock()) return;
  WifiDriverState &state = wifi_driver_state();
  std::string ssid;
  std::string password;
  uint8_t current_index = 0;
  uint8_t credential_count = 0;
  bool suspended = false;
  bool softap_active = false;
  int status_code = 0;
  bool station_connected = false;
  bool has_connected_once = false;
  {
    std::lock_guard<std::recursive_mutex> lock(state.mutex);
    ssid = state.ssid;
    password = state.password;
    current_index = state.current_credential_index;
    credential_count = state.credential_count;
    suspended = state.station_suspended || !state.station_enabled;
    softap_active = state.softap_active;
    status_code = state.status;
    station_connected = state.station_connected;
    has_connected_once = state.has_connected_once;
  }

  if (suspended) {
    ESP_LOGD(kWifiLogTag, "当前已暂停 STA 连接，跳过本次连接");
    return;
  }
  if (ssid.empty()) {
    LOGW(kWifiLogTag, "缺少 SSID，无法发起连接");
    return;
  }

  if (softap_active) {
    LOGW(kWifiLogTag,
         "主动调用 esp_wifi_set_mode(APSTA)：原因=connect_while_softap SSID=%s",
         ssid.c_str());
    const esp_err_t mode_error = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (mode_error != ESP_OK) {
      LOGE(kWifiLogTag,
           "切换到 APSTA 模式失败，无法在配网期间连接路由器：%s",
           esp_err_to_name(mode_error));
      return;
    }
  }

  // Calling esp_wifi_disconnect() while the station is already disconnected
  // emits a synthetic disconnect event. That stale event used to start the
  // retry timer and interrupt a newly submitted connection about one second
  // later. Disconnect only when replacing a live association.
  if (station_connected || status_code == kWifiStatusConnected) {
    LOGW(kWifiLogTag,
         "主动调用 esp_wifi_disconnect()：原因=replace_live_connection "
         "SSID=%s 当前状态=%d station_connected=%u has_connected_once=%u",
         ssid.c_str(), status_code, static_cast<unsigned>(station_connected),
         static_cast<unsigned>(has_connected_once));
    esp_wifi_disconnect();
  }

  wifi_config_t config = {};
  if (ssid.size() > sizeof(config.sta.ssid) ||
      password.size() > sizeof(config.sta.password)) {
    LOGE(kWifiLogTag, "Wi-Fi credentials exceed driver field capacity");
    return;
  }
  // IDF uses fixed-size byte fields; a full field needs no trailing NUL.
  std::memcpy(config.sta.ssid, ssid.data(), ssid.size());
  std::memcpy(config.sta.password, password.data(), password.size());
  config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
  config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
  config.sta.threshold.rssi = -85;
  config.sta.failure_retry_cnt = 3;
  const esp_err_t config_error = esp_wifi_set_config(WIFI_IF_STA, &config);
  if (config_error != ESP_OK) {
    LOGE(kWifiLogTag, "写入 STA 配置失败：%s", esp_err_to_name(config_error));
    return;
  }

  ESP_LOGD(kWifiLogTag, "准备连接 Wi-Fi：SSID=%s 序号=%u/%u", ssid.c_str(),
           static_cast<unsigned>(current_index),
           static_cast<unsigned>(credential_count));
  const esp_err_t connect_error = esp_wifi_connect();
  if (connect_error != ESP_OK) {
    LOGW(kWifiLogTag, "发起 Wi-Fi 连接失败：%s",
         esp_err_to_name(connect_error));
    return;
  }

  uint32_t attempt_number = 0;
  uint32_t disconnect_elapsed_ms = 0;
  int disconnect_reason = 0;
  const uint32_t now_ms =
      static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
  {
    std::lock_guard<std::recursive_mutex> lock(state.mutex);
    state.status = kWifiStatusIdle;
    state.current_candidate_tried = true;
    if (state.disconnect_started_ms != 0) {
      ++state.reconnect_attempts;
      attempt_number = state.reconnect_attempts;
      disconnect_elapsed_ms = now_ms - state.disconnect_started_ms;
      disconnect_reason = state.last_disconnect_reason;
    }
    state.last_reconnect_ms = now_ms;
  }
  if (attempt_number > 0) {
    LOGI(kWifiLogTag,
         "发起 Wi-Fi 重连：SSID=%s attempt=%u elapsed=%u ms reason=%d/%s",
         ssid.c_str(), static_cast<unsigned>(attempt_number),
         static_cast<unsigned>(disconnect_elapsed_ms), disconnect_reason,
         wifi_disconnect_reason_name(disconnect_reason));
  }
}

void wifi_driver_apply_pending_config() {
  WifiDriverState &state = wifi_driver_state();
  {
    std::lock_guard<std::recursive_mutex> lock(state.mutex);
    if (!state.pending_apply || state.station_connected) {
      return;
    }
    state.pending_apply = false;
    copy_wifi_credentials(state.pending_credentials,
                          state.pending_credential_count, state.credentials,
                          state.credential_count);
    reset_wifi_attempt_sequence(state.pending_preferred_index);
    state.first_connect_ms =
        static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
    state.was_connected = false;
    state.has_connected_once = false;
    wifi_driver_reset_reconnect_window_locked();
  }
  wifi_driver_begin_connection();
}
