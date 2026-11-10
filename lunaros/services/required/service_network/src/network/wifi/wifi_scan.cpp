#include "network/wifi/wifi_manager.h"

#include <algorithm>
#include <mutex>
#include <vector>

#include <esp_err.h>
#include <esp_wifi.h>

#include <esp_log.h>
#include "wifi_manager_internal.h"
#include "wifi_analysis_internal.h"

namespace {

WifiDriverState &state = wifi_driver_state();

} // namespace

static bool scan_access_points(std::vector<WifiScanApRecord> &records,
    uint16_t &total_count, bool survey, std::string &error) {
  constexpr const char *kTag = "wifi_scan";
  records.clear();
  error.clear();
  total_count = 0;
  std::unique_lock<std::recursive_mutex> radio_lock(wifi_radio_mutex(), std::try_to_lock);
  if (!radio_lock.owns_lock()) { error = "wifi_radio_busy"; return false; }
  if (lunaredu::lunaros::internal::wifi_monitor_active()) {
    error = "wifi_radio_busy"; return false;
  }

  if (!wifi_driver_ensure_stack_ready()) {
    error = "wifi_stack_not_ready";
    return false;
  }

  bool softap_active = false;
  bool started = false;
  {
    std::lock_guard<std::recursive_mutex> lock(state.mutex);
    if (!state.station_suspended && state.station_enabled &&
        state.credential_count > 0 && state.status == kWifiStatusIdle &&
        !state.station_connected) {
      error = "wifi_radio_busy";
      return false;
    }
    softap_active = state.softap_active;
    started = state.sta_started;
  }

  const wifi_mode_t target_mode =
      softap_active ? WIFI_MODE_APSTA : WIFI_MODE_STA;
  ESP_LOGW(kTag, "设置 Wi-Fi 扫描模式：softap_active=%u",
       static_cast<unsigned>(softap_active));
  const esp_err_t mode_error = esp_wifi_set_mode(target_mode);
  if (mode_error != ESP_OK) {
    error = esp_err_to_name(mode_error);
    ESP_LOGW(kTag, "Wi-Fi 扫描前设置模式失败：%s", error.c_str());
    return false;
  }

  if (!started) {
    const esp_err_t start_error = esp_wifi_start();
    if (start_error != ESP_OK && start_error != ESP_ERR_WIFI_CONN) {
      error = esp_err_to_name(start_error);
      ESP_LOGW(kTag, "Wi-Fi 扫描前启动 Wi-Fi 失败：%s", error.c_str());
      return false;
    }
    std::lock_guard<std::recursive_mutex> lock(state.mutex);
    state.sta_started = true;
  }

  wifi_scan_config_t scan_config = {};
  scan_config.show_hidden = survey;
  scan_config.scan_type = survey ? WIFI_SCAN_TYPE_PASSIVE : WIFI_SCAN_TYPE_ACTIVE;
  const esp_err_t scan_error = esp_wifi_scan_start(&scan_config, true);
  if (scan_error != ESP_OK) {
    error = esp_err_to_name(scan_error);
    ESP_LOGW(kTag, "Wi-Fi 扫描失败：%s", error.c_str());
    return false;
  }

  uint16_t ap_count = 0;
  const esp_err_t count_error = esp_wifi_scan_get_ap_num(&ap_count);
  if (count_error != ESP_OK) {
    esp_wifi_clear_ap_list();
    error = esp_err_to_name(count_error);
    return false;
  }
  total_count = ap_count;

  constexpr uint16_t kMaxScanResults = 24;
  ap_count = std::min<uint16_t>(ap_count, kMaxScanResults);
  if (ap_count == 0) {
    esp_wifi_clear_ap_list();
    return true;
  }

  std::vector<wifi_ap_record_t> ap_records(ap_count);
  const esp_err_t records_error =
      esp_wifi_scan_get_ap_records(&ap_count, ap_records.data());
  if (records_error != ESP_OK) {
    esp_wifi_clear_ap_list();
    error = esp_err_to_name(records_error);
    return false;
  }

  for (uint16_t index = 0; index < ap_count; ++index) {
    const wifi_ap_record_t &record = ap_records[index];
    const std::string ssid(reinterpret_cast<const char *>(record.ssid));
    if (ssid.empty() && !survey) {
      continue;
    }

    auto existing = std::find_if(
        records.begin(), records.end(),
        [&ssid](const WifiScanApRecord &item) { return item.ssid == ssid; });
    if (!survey && existing != records.end()) {
      if (record.rssi > existing->rssi) {
        std::copy_n(record.bssid, 6, existing->bssid.begin());
        existing->rssi = record.rssi;
        existing->authmode = static_cast<uint8_t>(record.authmode);
        existing->secure = record.authmode != WIFI_AUTH_OPEN;
        existing->channel = record.primary;
      }
      continue;
    }

    records.push_back({
        .ssid = ssid,
        .rssi = record.rssi,
        .authmode = static_cast<uint8_t>(record.authmode),
        .secure = record.authmode != WIFI_AUTH_OPEN,
        .channel = record.primary,
    });
    std::copy_n(record.bssid, 6, records.back().bssid.begin());
  }

  std::sort(records.begin(), records.end(),
            [](const WifiScanApRecord &left, const WifiScanApRecord &right) {
              return left.rssi > right.rssi;
            });
  return true;
}

bool wifi_scan_access_points(std::vector<WifiScanApRecord> &records,
                             std::string &error) {
  uint16_t ignored = 0;
  return scan_access_points(records, ignored, false, error);
}
bool wifi_survey_access_points(std::vector<WifiScanApRecord> &records,
                              uint16_t &total_count, std::string &error) {
  return scan_access_points(records, total_count, true, error);
}
