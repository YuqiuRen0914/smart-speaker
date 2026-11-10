#include "network/time/network_time.h"

#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>
#include <sys/time.h>

#include <esp_log.h>
#include <esp_netif_sntp.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>

namespace {

constexpr const char *kTag = "network_time";
constexpr const char *kTimeServers[] = {"ntp.aliyun.com", "time.cloudflare.com", "pool.ntp.org"};
constexpr uint64_t kSourceRetryUs = 20000000;
constexpr std::time_t kMinimumValidUnixTime = 1704067200; // 2024-01-01 UTC

std::mutex g_time_sync_mutex;
bool g_sntp_initialized = false;
bool g_sntp_requested = false;
size_t g_server_index = 0;
uint64_t g_server_started_us = 0;

void log_current_utc_time() {
  const std::time_t now = std::time(nullptr);
  std::tm utc = {};
  std::tm local = {};
  gmtime_r(&now, &utc);
  localtime_r(&now, &local);
  char utc_text[32] = {};
  char local_text[32] = {};
  if (std::strftime(utc_text, sizeof(utc_text), "%Y-%m-%dT%H:%M:%SZ", &utc) >
          0 &&
      std::strftime(local_text, sizeof(local_text), "%Y-%m-%dT%H:%M:%S",
                    &local) > 0) {
    const char *timezone = std::getenv("TZ");
    ESP_LOGI(kTag, "system time synchronized utc=%s local=%s tz=%s", utc_text,
             local_text, timezone == nullptr ? "UTC" : timezone);
  }
}

void on_time_synchronized(struct timeval *) { log_current_utc_time(); }

esp_err_t start_sntp_locked() {
  if (g_sntp_initialized) {
    return ESP_OK;
  }

  const char *server = kTimeServers[g_server_index];
  g_sntp_requested = true;
  g_server_started_us = static_cast<uint64_t>(esp_timer_get_time());
  esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(server);
  config.sync_cb = on_time_synchronized;
  const esp_err_t init_result = esp_netif_sntp_init(&config);
  if (init_result != ESP_OK && init_result != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(kTag, "SNTP init failed server=%s error=%s", server,
             esp_err_to_name(init_result));
    return init_result;
  }
  g_sntp_initialized = true;
  ESP_LOGI(kTag, "SNTP started server=%s", server);
  return ESP_OK;
}

} // namespace

esp_err_t network_set_system_timezone(const char *timezone_spec) {
  if (timezone_spec == nullptr || timezone_spec[0] == '\0' ||
      std::strlen(timezone_spec) > 63) {
    return ESP_ERR_INVALID_ARG;
  }
  if (setenv("TZ", timezone_spec, 1) != 0) {
    ESP_LOGE(kTag, "failed to configure timezone tz=%s", timezone_spec);
    return ESP_FAIL;
  }
  tzset();
  ESP_LOGI(kTag, "timezone configured tz=%s", timezone_spec);
  return ESP_OK;
}

esp_err_t network_start_system_time_sync() {
  std::lock_guard<std::mutex> lock(g_time_sync_mutex);
  return start_sntp_locked();
}

void network_poll_system_time_sync() {
  // Clock I/O may already be waiting on another owner. Never block Network
  // maintenance or interrupt that wait while rotating the fallback source.
  std::unique_lock<std::mutex> lock(g_time_sync_mutex, std::try_to_lock);
  if (!lock.owns_lock() || !g_sntp_requested || network_system_time_valid()) return;
  const auto current = static_cast<uint64_t>(esp_timer_get_time());
  if (current - g_server_started_us < kSourceRetryUs) return;
  if (g_sntp_initialized) esp_netif_sntp_deinit();
  g_sntp_initialized = false;
  g_server_index = (g_server_index + 1) % (sizeof(kTimeServers) / sizeof(kTimeServers[0]));
  (void)start_sntp_locked();
}

void network_stop_system_time_sync() {
  std::lock_guard<std::mutex> lock(g_time_sync_mutex);
  g_sntp_requested = false;
  if (!g_sntp_initialized) {
    return;
  }
  esp_netif_sntp_deinit();
  g_sntp_initialized = false;
}

bool network_system_time_valid() {
  return std::time(nullptr) >= kMinimumValidUnixTime;
}

esp_err_t network_sync_system_time(uint32_t timeout_ms, bool force) {
  if (!force && network_system_time_valid()) {
    return ESP_OK;
  }

  std::lock_guard<std::mutex> lock(g_time_sync_mutex);
  if (!force && network_system_time_valid()) {
    return ESP_OK;
  }

  if (force && g_sntp_initialized) {
    esp_netif_sntp_deinit();
    g_sntp_initialized = false;
  }

  const esp_err_t start_result = start_sntp_locked();
  if (start_result != ESP_OK) {
    return start_result;
  }

  const TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);
  const esp_err_t sync_result = esp_netif_sntp_sync_wait(timeout_ticks);
  if (sync_result != ESP_OK || !network_system_time_valid()) {
    ESP_LOGW(kTag, "system time sync failed error=%s valid=%d force=%d timeout_ms=%u",
             esp_err_to_name(sync_result), network_system_time_valid(),
             force, static_cast<unsigned>(timeout_ms));
    return sync_result == ESP_OK ? ESP_ERR_INVALID_STATE : sync_result;
  }

  log_current_utc_time();
  return ESP_OK;
}
