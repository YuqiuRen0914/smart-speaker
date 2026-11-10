#include "network/wifi/wifi_manager.h"

#include <cstring>
#include <mutex>
#include <string>

#include <esp_err.h>
#include <esp_netif.h>
#include <esp_wifi.h>

#include <esp_log.h>
#include "wifi_manager_internal.h"
#include "configuration/wifi_credentials.h"

namespace {

WifiDriverState &state = wifi_driver_state();
constexpr const char *kWifiLogTag = "wifi_softap";

uint32_t to_lwip_ip(uint32_t host_order_ip) {
  return ((host_order_ip & 0x000000FFu) << 24) |
         ((host_order_ip & 0x0000FF00u) << 8) |
         ((host_order_ip & 0x00FF0000u) >> 8) |
         ((host_order_ip & 0xFF000000u) >> 24);
}

const char *wifi_mode_name(wifi_mode_t mode) {
  switch (mode) {
  case WIFI_MODE_AP:
    return "AP";
  case WIFI_MODE_APSTA:
    return "APSTA";
  case WIFI_MODE_STA:
    return "STA";
  default:
    return "OTHER";
  }
}

void refresh_softap_ip() {
  std::string softap_ip;
  esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
  if (netif != nullptr) {
    esp_netif_ip_info_t ip_info = {};
    if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
      char buffer[20] = {};
      std::snprintf(buffer, sizeof(buffer), IPSTR, IP2STR(&ip_info.ip));
      softap_ip = buffer;
    }
  }
  std::lock_guard<std::recursive_mutex> lock(state.mutex);
  state.softap_ip = softap_ip;
}

} // namespace

bool wifi_configure_softap_network(uint32_t ip, uint32_t gateway,
                                   uint32_t subnet) {
  if (!wifi_driver_ensure_stack_ready()) {
    return false;
  }
  esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
  if (netif == nullptr) {
    return false;
  }

  esp_netif_ip_info_t ip_info = {};
  ip_info.ip.addr = to_lwip_ip(ip);
  ip_info.gw.addr = to_lwip_ip(gateway);
  ip_info.netmask.addr = to_lwip_ip(subnet);

  const auto stop = esp_netif_dhcps_stop(netif);
  if (stop != ESP_OK && stop != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) return false;
  const auto configured = esp_netif_set_ip_info(netif, &ip_info);
  const auto start = esp_netif_dhcps_start(netif);
  if (configured != ESP_OK ||
      (start != ESP_OK && start != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED)) return false;
  refresh_softap_ip();
  return true;
}

bool wifi_start_softap(const char *ssid, const char *password) {
  const std::string next_ssid = ssid ? std::string(ssid) : std::string();
  const std::string next_password = password ? std::string(password) : std::string();
  if (!lunaredu::lunaros::valid_device_wifi_credentials(next_ssid, next_password)) return false;
  if (!wifi_driver_ensure_stack_ready()) {
    return false;
  }

  bool wifi_started = false;
  bool station_suspended = false;
  bool keep_sta_radio = false;
  uint8_t radio_channel = 1;
  {
    std::lock_guard<std::recursive_mutex> lock(state.mutex);
    wifi_started = state.sta_started;
    station_suspended = state.station_suspended;
    keep_sta_radio = state.keep_sta_radio;
    radio_channel = state.radio_channel;
  }

  // A manual add/change flow uses APSTA and must preserve an existing station
  // association. Only the recovery/first-use pure-AP flow needs to stop the
  // running station before changing modes.
  if (wifi_started && station_suspended && !keep_sta_radio) {
    ESP_LOGW(kWifiLogTag,
         "主动调用 esp_wifi_stop()：原因=start_softap restart_required=1");
    const esp_err_t stop_error = esp_wifi_stop();
    if (stop_error != ESP_OK && stop_error != ESP_ERR_WIFI_NOT_INIT &&
        stop_error != ESP_ERR_WIFI_NOT_STARTED) {
      ESP_LOGE(kWifiLogTag, "启动 SoftAP 前停止 Wi-Fi 失败：%s",
           esp_err_to_name(stop_error));
      return false;
    }
    std::lock_guard<std::recursive_mutex> lock(state.mutex);
    state.sta_started = false;
    wifi_started = false;
  }

  const wifi_mode_t target_mode =
      station_suspended && !keep_sta_radio ? WIFI_MODE_AP : WIFI_MODE_APSTA;
  ESP_LOGW(kWifiLogTag,
       "主动调用 esp_wifi_set_mode(%s)：原因=start_softap station_suspended=%u",
       wifi_mode_name(target_mode), static_cast<unsigned>(station_suspended));
  const esp_err_t mode_error = esp_wifi_set_mode(target_mode);
  if (mode_error != ESP_OK) {
    ESP_LOGE(kWifiLogTag, "设置 SoftAP 模式失败：%s",
         esp_err_to_name(mode_error));
    return false;
  }

  wifi_config_t config = {};
  if (next_ssid.empty() || next_ssid.size() > sizeof(config.ap.ssid) ||
      next_password.size() > sizeof(config.ap.password)) {
    ESP_LOGE(kWifiLogTag, "SoftAP credentials exceed driver field capacity");
    return false;
  }
  std::memcpy(config.ap.ssid, next_ssid.data(), next_ssid.size());
  std::memcpy(config.ap.password, next_password.data(), next_password.size());
  config.ap.ssid_len = next_ssid.length();
  config.ap.channel = keep_sta_radio ? radio_channel : 1;
  config.ap.max_connection = 4;
  config.ap.ssid_hidden = 0;
  config.ap.beacon_interval = 100;
  config.ap.authmode =
      next_password.empty() ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA_WPA2_PSK;

  const esp_err_t config_error = esp_wifi_set_config(WIFI_IF_AP, &config);
  if (config_error != ESP_OK) {
    ESP_LOGE(kWifiLogTag, "写入 SoftAP 配置失败：%s",
         esp_err_to_name(config_error));
    return false;
  }

  if (!wifi_started) {
    const esp_err_t start_error = esp_wifi_start();
    if (start_error != ESP_OK) {
      ESP_LOGE(kWifiLogTag, "启动 SoftAP 失败：%s",
           esp_err_to_name(start_error));
      return false;
    }
  }
  wifi_driver_apply_power_save_setting();

  {
    std::lock_guard<std::recursive_mutex> lock(state.mutex);
    state.sta_started = true;
    state.softap_active = true;
  }
  ESP_LOGD(kWifiLogTag, "SoftAP 启动请求已发送：模式=%s SSID=%s 信道=%u",
           wifi_mode_name(target_mode), next_ssid.c_str(),
           static_cast<unsigned>(config.ap.channel));
  refresh_softap_ip();
  return true;
}

bool wifi_stop_softap() {
  bool ready;
  {
    std::lock_guard<std::recursive_mutex> lock(state.mutex);
    ready = state.stack_ready;
  }
  if (!ready) {
    std::string error;
    return wifi_driver_shutdown_stack(error); // Never initialize during rollback.
  }
  ESP_LOGW(kWifiLogTag, "主动调用 esp_wifi_set_mode(STA)：原因=stop_softap");
  const esp_err_t error = esp_wifi_set_mode(WIFI_MODE_STA);
  if (error != ESP_OK) {
    ESP_LOGW(kWifiLogTag, "停止 SoftAP 模式失败：%s", esp_err_to_name(error));
    return false;
  }
  bool should_start = false;
  bool should_reconnect = false;
  {
    std::lock_guard<std::recursive_mutex> lock(state.mutex);
    state.softap_active = false;
    state.softap_ip = "";
    should_start = !state.sta_started;
    should_reconnect = error == ESP_OK && !state.station_suspended &&
                       state.credential_count > 0 && !wifi_connected();
  }
  if (should_start) {
    ESP_LOGW(kWifiLogTag, "主动调用 esp_wifi_start()：原因=stop_softap_restart_sta");
    const auto start_error = esp_wifi_start();
    if (start_error != ESP_OK) return false;
    wifi_driver_apply_power_save_setting();
    std::lock_guard<std::recursive_mutex> lock(state.mutex);
    state.sta_started = true;
  }
  if (should_reconnect) {
    wifi_driver_begin_connection();
  }
  return true;
}
