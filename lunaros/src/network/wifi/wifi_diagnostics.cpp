#include "network/wifi/wifi_manager_internal.h"

#include <cstdio>
#include <mutex>
#include <string>

#include <esp_err.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_timer.h>
#include <esp_wifi.h>

#include "network/wifi/wifi_manager.h"
#include "network/wifi/wifi_reason.h"

namespace {

constexpr const char *kWifiLogTag = "wifi_diagnostics";

std::string ip_info_to_string(const esp_netif_ip_info_t &ip_info) {
  char buffer[20];
  std::snprintf(buffer, sizeof(buffer), IPSTR, IP2STR(&ip_info.ip));
  return std::string(buffer);
}

void refresh_sta_ip() {
  std::string local_ip;
  esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  if (netif != nullptr) {
    esp_netif_ip_info_t ip_info = {};
    if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
      local_ip = ip_info_to_string(ip_info);
    }
  }
  WifiDriverState &state = wifi_driver_state();
  std::lock_guard<std::recursive_mutex> lock(state.mutex);
  state.local_ip = local_ip;
}

} // namespace

void wifi_driver_update_rssi_snapshot() {
  wifi_ap_record_t record = {};
  if (esp_wifi_sta_get_ap_info(&record) == ESP_OK) {
    WifiDriverState &state = wifi_driver_state();
    std::lock_guard<std::recursive_mutex> lock(state.mutex);
    state.last_rssi = record.rssi;
    state.last_rssi_sample_ms =
        static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
  }
}

void wifi_driver_log_status() {
  WifiDriverState &state = wifi_driver_state();
  const bool connected = wifi_connected();
  if (connected) {
    refresh_sta_ip();
    wifi_driver_update_rssi_snapshot();
    const WifiRuntimeSnapshot snapshot = wifi_runtime_snapshot();
    ESP_LOGD(kWifiLogTag, "Wi-Fi 已连接：SSID=%s IP=%s RSSI=%ld",
             snapshot.target_ssid.c_str(), snapshot.local_ip.c_str(),
             static_cast<long>(snapshot.rssi));
    return;
  }

  const WifiRuntimeSnapshot snapshot = wifi_runtime_snapshot();
  uint8_t credential_count = 0;
  int last_reason = 0;
  {
    std::lock_guard<std::recursive_mutex> lock(state.mutex);
    credential_count = state.credential_count;
    last_reason = state.last_disconnect_reason;
  }
  ESP_LOGD(kWifiLogTag, "Wi-Fi 未连接：状态=%d", snapshot.status_code);
  ESP_LOGD(kWifiLogTag,
           "目标 SSID=%s 凭据数=%u 最近原因=%d/%s 重连次数=%u 下次延迟=%u ms",
           snapshot.target_ssid.empty() ? "(none)"
                                        : snapshot.target_ssid.c_str(),
           static_cast<unsigned>(credential_count), last_reason,
           wifi_disconnect_reason_name(last_reason),
           static_cast<unsigned>(snapshot.reconnect_attempts),
           static_cast<unsigned>(snapshot.next_reconnect_delay_ms));
}
