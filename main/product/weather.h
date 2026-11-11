#pragma once
#include <cstdint>
#include <string>

namespace smart_speaker::product {
struct WeatherConfig {
  bool enabled = false;
  std::string place;
  double latitude = 0;
  double longitude = 0;
};
struct WeatherReading {
  float temperature_c = 0;
  uint8_t humidity_percent = 0;
  uint8_t code = 0;
  int64_t observed_epoch = 0;
};
struct WeatherSnapshot {
  WeatherConfig config;
  WeatherReading reading;
  bool has_reading = false;
  bool refreshing = false;
  bool retry_required = false;
  uint32_t age_seconds = 0;
  uint32_t revision = 0;
  std::string status = "not_configured";
  std::string error;
};
bool weather_config_valid(const WeatherConfig &config);
bool weather_decode(const std::string &body, int64_t now_epoch,
                    WeatherReading &reading, std::string &error);
const char *weather_code_label(uint8_t code);
// Boot/config writes execute outside UI/audio tasks. No display or HTTP owner
// keeps another mutable weather state. Samples are intentionally not persisted.
bool weather_load();
bool weather_configure(const WeatherConfig &config, std::string &error);
bool weather_request_refresh(std::string &error);
void weather_poll(uint32_t now_ms, int64_t epoch, bool trusted, bool online);
WeatherSnapshot weather_snapshot(uint32_t now_ms, int64_t epoch, bool trusted, bool online);
} // namespace smart_speaker::product
