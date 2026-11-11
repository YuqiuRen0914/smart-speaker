#include "weather.h"
#include "lunaredu_lunaros/network/http_client.h"
#include "lunaredu_lunaros/network/network_jobs.h"
#include "lunaredu_lunaros/persistence/storage.h"
#include <ArduinoJson.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <ctime>

namespace smart_speaker::product {
namespace {
using namespace lunaredu::lunaros;
constexpr const char *kPath = "/user/product_weather.json";
constexpr uint32_t kRefreshMs = 15 * 60 * 1000;
constexpr uint32_t kStaleSeconds = 60 * 60;
std::mutex g_mutex, g_config_io;
WeatherSnapshot g_state;
NetworkJobHandle g_job;
uint32_t g_generation = 0, g_received_at = 0, g_attempt_at = 0, g_now = 0;
uint32_t g_initial_age = 0;
bool g_trusted = false, g_online = false;
bool g_attempted = false, g_requested = false;
uint8_t g_failures = 0;

bool known_code(int code) {
  switch (code) {
    case 0: case 1: case 2: case 3: case 45: case 48:
    case 51: case 53: case 55: case 56: case 57: case 61: case 63: case 65:
    case 66: case 67: case 71: case 73: case 75: case 77: case 80: case 81:
    case 82: case 85: case 86: case 95: case 96: case 99: return true;
    default: return false;
  }
}
NetworkJobRunResult fetch_weather(NetworkJobHandle handle, void *) {
  WeatherConfig config;
  uint32_t generation;
  {
    std::lock_guard lock(g_mutex);
    if (!g_trusted || !g_online) {
      g_state.refreshing = false;
      g_state.error = !g_online ? "weather_offline" : "weather_time_untrusted";
      return {false, false, g_state.error};
    }
    config = g_state.config; generation = g_generation;
  }
  std::string error;
  WeatherReading reading;
  NetworkResponse response;
  bool ok = false;
  if (!config.enabled || network_job_cancellation_requested(handle)) error = "weather_cancelled";
  else {
    char query[320];
    // Fixed public TLS origin; coordinates cannot inject a URL or credentials.
    std::snprintf(query, sizeof(query),
        "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f&current=temperature_2m,relative_humidity_2m,weather_code&temperature_unit=celsius&timeformat=unixtime&timezone=GMT",
        config.latitude, config.longitude);
    NetworkFetchRequest request;
    request.url = query; request.timeout_ms = 5000; request.maximum_response_bytes = 4096;
    request.transport_security = NetworkTransportSecurity::RequireTls;
    const bool fetched = network_fetch_resource(std::move(request), &response);
    if (network_job_cancellation_requested(handle)) error = "weather_cancelled";
    else if (!fetched) error = "weather_transport_failed";
    else if (response.status_code != 200) error = "weather_http_" + std::to_string(response.status_code);
    else ok = weather_decode(response.body, static_cast<int64_t>(std::time(nullptr)), reading, error);
  }
  std::lock_guard lock(g_mutex);
  g_state.refreshing = false;
  if (generation != g_generation) return {false, false, "weather_location_changed"};
  if (ok) {
    g_state.reading = reading; g_state.has_reading = true;
    g_received_at = g_now;
    g_initial_age = static_cast<uint32_t>(std::max<int64_t>(std::time(nullptr) - reading.observed_epoch, 0));
    g_failures = 0; g_state.error.clear();
  } else {
    if (g_failures < 3) ++g_failures;
    g_state.error = error;
  }
  g_state.retry_required = g_failures >= 3;
  ++g_state.revision;
  return {ok, false, ok ? "weather_updated" : error};
}
} // namespace

bool weather_config_valid(const WeatherConfig &c) {
  if (!std::isfinite(c.latitude) || !std::isfinite(c.longitude) ||
      c.latitude < -90 || c.latitude > 90 || c.longitude < -180 || c.longitude > 180 ||
      c.place.size() > 64 || (c.enabled && c.place.empty())) return false;
  for (unsigned char ch : c.place) if (ch < 0x20 || ch == 0x7f) return false;
  return true;
}

bool weather_decode(const std::string &body, int64_t now, WeatherReading &result, std::string &error) {
  JsonDocument doc;
  error = "weather_response_invalid";
  if (body.size() > 4096 || deserializeJson(doc, body, DeserializationOption::NestingLimit(4))) return false;
  const JsonObjectConst current = doc["current"].as<JsonObjectConst>();
  const JsonObjectConst units = doc["current_units"].as<JsonObjectConst>();
  if (!current["temperature_2m"].is<float>() || !current["relative_humidity_2m"].is<int>() ||
      !current["weather_code"].is<int>() || !current["time"].is<int64_t>() ||
      units["temperature_2m"] != "°C" || units["relative_humidity_2m"] != "%" ||
      units["time"] != "unixtime" || units["weather_code"] != "wmo code") return false;
  const float temperature = current["temperature_2m"].as<float>();
  const int humidity = current["relative_humidity_2m"].as<int>();
  const int code = current["weather_code"].as<int>();
  const int64_t observed = current["time"].as<int64_t>();
  if (!std::isfinite(temperature) || temperature < -100 || temperature > 70 ||
      humidity < 0 || humidity > 100 || !known_code(code) || observed < 1577836800 ||
      observed > now + 300 || now - observed > 86400) return false;
  result = {temperature, static_cast<uint8_t>(humidity), static_cast<uint8_t>(code), observed};
  error.clear(); return true;
}

const char *weather_code_label(uint8_t code) {
  if (code == 0) return "晴";
  if (code <= 3) return code == 3 ? "阴" : "多云";
  if (code == 45 || code == 48) return "雾";
  if (code >= 51 && code <= 57) return "毛毛雨";
  if (code >= 61 && code <= 67) return "雨";
  if (code >= 71 && code <= 77) return "雪";
  if (code >= 80 && code <= 82) return "阵雨";
  if (code == 85 || code == 86) return "阵雪";
  if (code >= 95 && code <= 99) return "雷雨";
  return "未知";
}

bool weather_load() {
  std::lock_guard io(g_config_io);
  WeatherConfig config;
  std::string text;
  bool ok = storage_ready();
  if (ok && storage_path_exists(kPath)) {
    ok = storage_stream_file(kPath, [](const uint8_t *data, size_t size, void *ctx) {
      auto &value = *static_cast<std::string *>(ctx);
      if (size > 512 - value.size()) return false;
      value.append(reinterpret_cast<const char *>(data), size); return true;
    }, &text);
    JsonDocument doc;
    ok = ok && !deserializeJson(doc, text, DeserializationOption::NestingLimit(2)) &&
        doc["schema"] == 1 && doc["enabled"].is<bool>() && doc["place"].is<const char *>() &&
        doc["latitude"].is<double>() && doc["longitude"].is<double>();
    if (ok) {
      config = {doc["enabled"].as<bool>(), doc["place"].as<std::string>(),
          doc["latitude"].as<double>(), doc["longitude"].as<double>()};
      ok = weather_config_valid(config);
    }
  }
  std::lock_guard lock(g_mutex);
  ++g_generation;
  g_state = {}; g_state.config = ok ? config : WeatherConfig{};
  g_state.error = ok ? "" : "weather_config_read_failed";
  g_attempted = false; g_requested = false; g_failures = 0;
  return ok;
}

bool weather_configure(const WeatherConfig &config, std::string &error) {
  if (!weather_config_valid(config)) { error = "weather_config_invalid"; return false; }
  std::lock_guard io(g_config_io);
  JsonDocument doc;
  doc["schema"] = 1; doc["enabled"] = config.enabled; doc["place"] = config.place;
  doc["latitude"] = config.latitude; doc["longitude"] = config.longitude;
  std::string text; serializeJson(doc, text);
  if (!storage_ready() || !storage_write_text_file(kPath, text.c_str())) {
    error = "weather_config_write_failed"; return false;
  }
  std::lock_guard lock(g_mutex);
  const bool pending = g_state.refreshing;
  const auto revision = g_state.revision + 1;
  g_state = {}; g_state.config = config; g_state.refreshing = pending; g_state.revision = revision;
  ++g_generation; g_failures = 0; g_attempted = false; g_requested = false;
  g_initial_age = 0;
  error.clear(); return true;
}

bool weather_request_refresh(std::string &error) {
  std::lock_guard lock(g_mutex);
  if (!g_state.config.enabled) { error = "weather_not_configured"; return false; }
  if (g_state.refreshing || g_requested) { error = "weather_refresh_pending"; return false; }
  g_requested = true; error.clear(); return true;
}

void weather_poll(uint32_t now, int64_t epoch, bool trusted, bool online) {
  std::lock_guard lock(g_mutex);
  (void)epoch;
  g_now = now; g_trusted = trusted; g_online = online;
  if (g_state.refreshing) {
    if (network_job_active(g_job)) return;
    // A queued job may be cancelled by maintenance before its runner executes.
    g_state.refreshing = false; g_state.error = "weather_cancelled";
    if (g_failures < 3) ++g_failures;
    g_state.retry_required = g_failures >= 3;
  }
  if (!g_state.config.enabled || !trusted || !online) return;
  const uint32_t interval = g_failures ? (g_failures == 1 ? 60'000U : 300'000U) : kRefreshMs;
  // Manual requests also observe the minimum 60-second rate limit.
  if (g_attempted && now - g_attempt_at < (g_requested ? 60'000U : interval)) return;
  if (g_failures >= 3 && !g_requested) return;
  g_requested = false;
  NetworkJobDefinition job;
  job.owner_service_id = "product.weather"; job.kind = "weather.fetch";
  job.priority = kNetworkPriorityBackground; job.maximum_attempts = 1;
  job.runner = fetch_weather;
  std::string error;
  g_attempt_at = now; g_attempted = true;
  if (!submit_network_job(job, g_job, error)) {
    g_state.error = "weather_queue_unavailable";
    if (g_failures < 3) ++g_failures;
    g_state.retry_required = g_failures >= 3;
    return;
  }
  g_state.refreshing = true;
}

WeatherSnapshot weather_snapshot(uint32_t now, int64_t epoch, bool trusted, bool online) {
  std::lock_guard lock(g_mutex);
  auto result = g_state;
  result.refreshing = result.refreshing || g_requested;
  if (result.has_reading) {
    // Monotonic age remains meaningful even while NTP is temporarily invalid.
    const uint64_t elapsed = uint64_t((now - g_received_at) / 1000) + g_initial_age;
    const int64_t wall_age = trusted && epoch > result.reading.observed_epoch ? epoch - result.reading.observed_epoch : 0;
    const uint64_t age = std::max<uint64_t>(elapsed, wall_age);
    result.age_seconds = static_cast<uint32_t>(std::min<uint64_t>(age, UINT32_MAX));
  }
  result.status = !result.config.enabled ? (result.error.empty() ? "not_configured" : "error") : !trusted ? "time_untrusted" :
      !online ? "offline" : result.has_reading && result.age_seconds >= kStaleSeconds ? "stale" :
      !result.error.empty() ? "error" : result.has_reading ? "fresh" : result.refreshing ? "loading" : "waiting";
  return result;
}
} // namespace smart_speaker::product
