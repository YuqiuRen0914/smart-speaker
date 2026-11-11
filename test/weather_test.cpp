#include "weather.h"
#include "lunaredu_lunaros/network/http_client.h"
#include "lunaredu_lunaros/network/network_jobs.h"
#include "host/product_time_storage_test.h"
#include <ArduinoJson.h>
#include <cassert>
#include <cmath>
#include <ctime>
#include <functional>
#include <iostream>
#include <limits>

using namespace smart_speaker::product;
namespace {
using namespace lunaredu::lunaros;
NetworkJobDefinition queued;
NetworkJobHandle active;
uint32_t submissions = 0, fetches = 0;
bool fail_submit = false, transport_ok = true, cancelled = false;
std::function<void()> during_fetch;
NetworkResponse reply;
std::string response(int64_t time, float temperature = 0, int humidity = 0, int code = 0) {
  JsonDocument doc;
  doc["current_units"]["time"] = "unixtime";
  doc["current_units"]["temperature_2m"] = "°C";
  doc["current_units"]["relative_humidity_2m"] = "%";
  doc["current_units"]["weather_code"] = "wmo code";
  doc["current"]["time"] = time; doc["current"]["temperature_2m"] = temperature;
  doc["current"]["relative_humidity_2m"] = humidity; doc["current"]["weather_code"] = code;
  std::string text; serializeJson(doc, text); return text;
}
void run() { assert(active); auto job = queued; auto handle = active; job.runner(handle, job.context); active = {}; }
}
namespace lunaredu::lunaros {
bool submit_network_job(const NetworkJobDefinition &job, NetworkJobHandle &handle, std::string &) {
  if (fail_submit) return false;
  assert(!active); queued = job; handle.id = ++submissions; active = handle; return true;
}
bool network_job_active(NetworkJobHandle handle) { return active && active.id == handle.id; }
bool network_job_cancellation_requested(NetworkJobHandle) { return cancelled; }
bool network_fetch_resource(NetworkFetchRequest request, NetworkResponse *result) {
  ++fetches;
  assert(request.url.rfind("https://api.open-meteo.com/v1/forecast?latitude=", 0) == 0);
  assert(request.maximum_response_bytes == 4096 && request.timeout_ms == 5000);
  assert(request.transport_security == NetworkTransportSecurity::RequireTls);
  if (during_fetch) during_fetch();
  *result = reply; return transport_ok;
}
}
int main() {
  const int64_t now = std::time(nullptr);
  std::string error;
  WeatherReading reading{15, 23, 3, now};
  assert(weather_decode(response(now - 900), now, reading, error));
  assert(reading.temperature_c == 0 && reading.humidity_percent == 0 && reading.code == 0);
  const auto valid = reading;
  for (const auto &body : {std::string("{}"), response(now + 301), response(now - 86401),
      response(now, 71), response(now, -101), response(now, 20, 101), response(now, 20, 30, 4),
      std::string(4097, 'x')}) {
    assert(!weather_decode(body, now, reading, error));
    assert(reading.observed_epoch == valid.observed_epoch && reading.temperature_c == 0);
  }
  auto bad_unit = response(now); bad_unit.replace(bad_unit.find("°C"), 3, "°F");
  assert(!weather_decode(bad_unit, now, reading, error));
  assert(!weather_config_valid({true, "", 0, 0}));
  assert(!weather_config_valid({true, "place", 91, 0}));
  assert(!weather_config_valid({true, "place", 0, std::numeric_limits<double>::quiet_NaN()}));
  assert(!weather_config_valid({true, "name\n", 0, 0}));
  test_storage::reset(); assert(weather_load());
  auto snap = weather_snapshot(0, now, true, true);
  assert(!snap.has_reading && snap.status == "not_configured");
  assert(!weather_request_refresh(error));
  assert(weather_configure({true, "Shanghai", 31.23, 121.47}, error));
  const auto config_record = test_storage::record;
  test_storage::fail_write = true;
  assert(!weather_configure({true, "Other", 0, 0}, error));
  assert(weather_snapshot(0, now, true, true).config.place == "Shanghai");
  assert(test_storage::record == config_record);
  test_storage::fail_write = false;
  weather_poll(1000, now, false, true); assert(!active);
  weather_poll(1000, now, true, false); assert(!active);
  weather_poll(1000, now, true, true); assert(active);
  weather_poll(2000, now, true, true); assert(submissions == 1);
  reply.status_code = 200; reply.body = response(now - 900, 7.2f, 82, 2);
  run();
  snap = weather_snapshot(2000, now, true, true);
  assert(snap.status == "fresh" && snap.has_reading && std::fabs(snap.reading.temperature_c - 7.2f) < 0.01f);
  assert(snap.age_seconds >= 900 && snap.age_seconds <= 905);
  assert(weather_snapshot(2000, now, true, false).status == "offline");
  assert(weather_snapshot(3'602'000, now + 3600, true, true).status == "stale");
  assert(weather_snapshot(3'602'000, now, false, true).status == "time_untrusted");
  weather_poll(3000, now, true, true); assert(!active);
  assert(weather_request_refresh(error));
  assert(!weather_request_refresh(error));
  weather_poll(60'999, now, true, true); assert(!active);
  weather_poll(61'000, now, true, true); assert(active);
  // An explicit location replacement during the old HTTP call cannot publish
  // a sample for the old coordinates beneath the new location's name.
  during_fetch = [&] { assert(weather_configure({true, "New place", 0, 0}, error)); };
  run(); during_fetch = {};
  snap = weather_snapshot(61'000, now, true, true);
  assert(!snap.has_reading && snap.config.place == "New place");
  weather_poll(62'000, now, true, true); assert(active);
  transport_ok = false; run();
  weather_poll(121'999, now, true, true); assert(!active);
  weather_poll(122'000, now, true, true); assert(active); run();
  weather_poll(422'000, now, true, true); assert(active); run();
  assert(weather_snapshot(422'000, now, true, true).retry_required);
  const auto count = submissions;
  weather_poll(2'000'000, now, true, true); assert(!active && submissions == count);
  assert(weather_request_refresh(error)); weather_poll(2'000'001, now, true, true); assert(active);
  transport_ok = true; run(); assert(!weather_snapshot(2'000'001, now, true, true).retry_required);
  assert(weather_load()); // reload durable config; no weather cache survives reboot
  assert(!weather_snapshot(0, now, true, true).has_reading);
  assert(weather_snapshot(0, now, true, true).config.place == "New place");
  weather_poll(10, now, true, true); assert(active);
  active = {}; // queued cancellation before runner
  weather_poll(11, now, true, true);
  assert(weather_snapshot(11, now, true, true).error == "weather_cancelled");
  test_storage::record = std::string(513, 'x'); assert(!weather_load());
  assert(weather_snapshot(0, now, true, true).error == "weather_config_read_failed");
  assert(test_storage::record.size() == 513); // preserve corrupt file for recovery
  assert(weather_configure({false, "", 0, 0}, error));
  weather_poll(3'000'000, now, true, true); assert(!active);
  assert(!weather_request_refresh(error));
  std::cout << "Weather: real-schema parsing, units/age, persistent rollback, offline, rate limit, bounded retry and location generation passed\n";
}
