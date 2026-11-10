#include "network/wifi/wifi_manager_internal.h"

#include <mutex>

#include <esp_err.h>
#include <esp_netif.h>
#include <esp_wifi.h>

#include <esp_log.h>

namespace {

constexpr const char *kWifiLogTag = "wifi_stack";
esp_netif_t *g_station_netif = nullptr;
esp_netif_t *g_access_point_netif = nullptr;
bool g_created_station_netif = false;
bool g_created_access_point_netif = false;
// Readiness is not ownership. Retain partial resources until cleanup succeeds.
bool g_wifi_initialized = false;

const char *wifi_power_save_name(wifi_ps_type_t power_save) {
  switch (power_save) {
  case WIFI_PS_NONE:
    return "WIFI_PS_NONE";
  case WIFI_PS_MIN_MODEM:
    return "WIFI_PS_MIN_MODEM";
  case WIFI_PS_MAX_MODEM:
    return "WIFI_PS_MAX_MODEM";
  default:
    return "UNKNOWN";
  }
}

} // namespace

bool wifi_driver_apply_power_save_setting() {
  WifiDriverState &state = wifi_driver_state();
  wifi_ps_type_t power_save = WIFI_PS_MIN_MODEM;
  {
    std::lock_guard<std::recursive_mutex> lock(state.mutex);
    power_save = state.power_save_type;
  }

  const esp_err_t error = esp_wifi_set_ps(power_save);
  if (error != ESP_OK) {
    ESP_LOGW(kWifiLogTag, "应用 Wi-Fi 省电级别失败：%s",
         esp_err_to_name(error));
    return false;
  }
  ESP_LOGI(kWifiLogTag, "Wi-Fi 省电级别已设置：%s",
       wifi_power_save_name(power_save));
  return true;
}

bool wifi_driver_ensure_stack_ready() {
  WifiDriverState &state = wifi_driver_state();
  {
    std::lock_guard<std::recursive_mutex> lock(state.mutex);
    if (state.stack_ready) {
      return true;
    }
  }

  std::string cleanup_error;
  if (g_wifi_initialized || g_station_netif || g_access_point_netif) {
    if (!wifi_driver_shutdown_stack(cleanup_error)) return false;
  }
  const auto failed = [&]() {
    if (!wifi_driver_shutdown_stack(cleanup_error)) {
      ESP_LOGW(kWifiLogTag, "Wi-Fi setup cleanup pending: %s", cleanup_error.c_str());
    }
    return false;
  };
  g_station_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  if (g_station_netif == nullptr) {
    g_station_netif = esp_netif_create_default_wifi_sta();
    g_created_station_netif = g_station_netif != nullptr;
  }
  g_access_point_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
  if (g_access_point_netif == nullptr) {
    g_access_point_netif = esp_netif_create_default_wifi_ap();
    g_created_access_point_netif = g_access_point_netif != nullptr;
  }
  if (g_station_netif == nullptr || g_access_point_netif == nullptr) {
    ESP_LOGE(kWifiLogTag, "创建默认 Wi-Fi 网络接口失败");
    return failed();
  }

  wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
  // OS owns credentials and uses WIFI_STORAGE_RAM. A damaged business NVS
  // partition must not prevent the recovery installer from bringing up Wi-Fi.
  config.nvs_enable = 0;
  const esp_err_t init_error = esp_wifi_init(&config);
  if (init_error != ESP_OK && init_error != ESP_ERR_INVALID_STATE &&
      init_error != ESP_ERR_WIFI_INIT_STATE) {
    ESP_LOGE(kWifiLogTag, "初始化 Wi-Fi 失败：%s", esp_err_to_name(init_error));
    return failed();
  }
  g_wifi_initialized = true;
  const esp_err_t storage_error = esp_wifi_set_storage(WIFI_STORAGE_RAM);
  if (storage_error != ESP_OK) {
    ESP_LOGE(kWifiLogTag, "设置 Wi-Fi 存储模式失败：%s",
         esp_err_to_name(storage_error));
    return failed();
  }
  if (!wifi_driver_register_event_handlers()) {
    return failed();
  }

  std::lock_guard<std::recursive_mutex> lock(state.mutex);
  state.stack_ready = true;
  return true;
}

bool wifi_driver_shutdown_stack(std::string &error) {
  WifiDriverState &state = wifi_driver_state();
  error.clear();
  {
    std::lock_guard<std::recursive_mutex> lock(state.mutex);
    state.stack_ready = false;
  }

  if (g_wifi_initialized) {
    (void)esp_wifi_disconnect();
    const esp_err_t stop_error = esp_wifi_stop();
    if (stop_error != ESP_OK && stop_error != ESP_ERR_WIFI_NOT_INIT &&
        stop_error != ESP_ERR_WIFI_NOT_STARTED) {
      error = std::string("wifi_stop_failed:") + esp_err_to_name(stop_error);
      return false;
    }
    // SDK stop is already complete even if later handler/deinit cleanup fails.
    std::lock_guard<std::recursive_mutex> lock(state.mutex);
    state.sta_started = state.softap_active = state.station_connected = false;
    state.status = kWifiStatusDisconnected;
    state.local_ip.clear();
    state.softap_ip.clear();
    state.station_connected_since_ms = 0;
  }
  if (!wifi_driver_unregister_event_handlers(error)) return false;
  if (g_wifi_initialized) {
    const esp_err_t deinit_error = esp_wifi_deinit();
    if (deinit_error != ESP_OK && deinit_error != ESP_ERR_WIFI_NOT_INIT) {
      error = std::string("wifi_deinit_failed:") + esp_err_to_name(deinit_error);
      return false;
    }
    g_wifi_initialized = false;
  }

  if (g_created_station_netif && g_station_netif != nullptr) {
    esp_netif_destroy_default_wifi(g_station_netif);
  }
  if (g_created_access_point_netif && g_access_point_netif != nullptr) {
    esp_netif_destroy_default_wifi(g_access_point_netif);
  }
  g_station_netif = nullptr;
  g_access_point_netif = nullptr;
  g_created_station_netif = false;
  g_created_access_point_netif = false;

  std::lock_guard<std::recursive_mutex> lock(state.mutex);
  state.status = kWifiStatusDisconnected;
  state.local_ip.clear();
  state.softap_ip.clear();
  state.ssid.clear();
  state.password.clear();
  state.station_connected = false;
  state.was_connected = false;
  state.station_connected_since_ms = 0;
  state.station_suspended = false;
  state.sta_started = false;
  state.softap_active = false;
  state.stack_ready = false;
  state.pending_apply = false;
  state.pending_disconnect = false;
  state.pending_disconnect_checked_ms = 0;
  wifi_driver_reset_reconnect_window_locked();
  return true;
}
