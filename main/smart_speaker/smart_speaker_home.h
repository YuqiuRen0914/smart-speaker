#pragma once

#include <ctime>
#include <cstdint>
#include <string>
#include <lvgl.h>
#include "product/weather.h"

namespace smart_speaker {

// Render-only data. Clock, weather and wallpaper ownership stays with the
// existing services; this view never starts timers, reads SD or requests HTTP.
struct HomeContent {
  std::string date, weekday, time, seconds, place, temperature, humidity;
  std::string status;
  int weather_code = -1;
  int temperature_progress = 0, humidity_progress = 0;
};
HomeContent home_content(const std::tm *trusted_local_time,
                         const product::WeatherSnapshot &weather,
                         bool background_missing);

class SpeakerHome {
 public:
  void create(lv_obj_t *parent, const lv_font_t *clock_font,
              const lv_font_t *seconds_font,
              const lv_font_t *body_font, const lv_font_t *tiny_font,
              const lv_font_t *heading_font);
  void show(const HomeContent &content, bool has_background);
  void hide();
  // Presentation only: the resident display owner drives one opening timeline.
  // Negative settle_ms holds the centered clock while the network is pending.
  void set_boot_frame(uint32_t elapsed_ms, int32_t settle_ms);
  void finish_boot();

 private:
  void apply_boot_frame();
  static void draw_boot_curtain(lv_event_t *event);
  lv_obj_t *root_ = nullptr;
  lv_obj_t *clock_clip_ = nullptr, *header_clip_ = nullptr;
  lv_obj_t *date_clip_ = nullptr, *metrics_clip_ = nullptr, *seconds_clip_ = nullptr;
  lv_obj_t *date_ = nullptr, *weekday_ = nullptr;
  lv_obj_t *hours_ = nullptr, *minutes_ = nullptr, *seconds_ = nullptr;
  lv_obj_t *place_ = nullptr, *temperature_ = nullptr, *humidity_ = nullptr;
  lv_obj_t *status_ = nullptr;
  lv_obj_t *icon_ = nullptr, *temperature_bar_ = nullptr, *humidity_bar_ = nullptr;
  int weather_code_ = -1;
  int clock_height_ = 0, seconds_height_ = 0;
  bool boot_frame_ = false, has_background_ = false;
  uint32_t boot_elapsed_ms_ = 0;
  int32_t boot_settle_ms_ = -1;
  int curtain_top_ = 256;
};

} // namespace smart_speaker
