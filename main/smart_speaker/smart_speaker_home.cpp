// Product home face based on the user's SmallTV weather-clock reference.
// The reference firmware supplies no UI source. This implementation uses our
// existing LVGL owner, weather service, wallpaper cache and licensed fonts.
#include "smart_speaker_home.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace smart_speaker {
namespace {
constexpr uint32_t kWhite = 0xFFFFFF, kYellow = 0xFFD426;
constexpr uint32_t kGreen = 0x8CDD50, kCyan = 0x53CAEF;
float ease(uint32_t elapsed, uint32_t delay, uint32_t duration) {
  const float t = elapsed <= delay ? 0.0f :
      std::min(1.0f, static_cast<float>(elapsed - delay) / duration);
  return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
}
lv_obj_t *clip(lv_obj_t *parent, int x, int y, int width, int height) {
  auto *object = lv_obj_create(parent);
  lv_obj_remove_style_all(object);
  lv_obj_set_pos(object, x, y);
  lv_obj_set_size(object, width, height);
  lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(object, LV_OBJ_FLAG_CLICKABLE);
  return object;
}
void reveal(lv_obj_t *object, int height, float progress) {
  if (progress <= 0) lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
  else {
    lv_obj_remove_flag(object, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_height(object, std::max(1, static_cast<int>(std::lround(height * progress))));
  }
}
void text(lv_obj_t *label, const std::string &value) {
  if (value != lv_label_get_text(label)) lv_label_set_text(label, value.c_str());
}
lv_obj_t *label(lv_obj_t *parent, int x, int y, int width,
                const lv_font_t *font, uint32_t color,
                lv_text_align_t align = LV_TEXT_ALIGN_LEFT) {
  auto *object = lv_label_create(parent);
  lv_label_set_text(object, "");
  lv_label_set_long_mode(object, LV_LABEL_LONG_DOT);
  lv_obj_set_size(object, width, lv_font_get_line_height(font));
  lv_obj_set_pos(object, x, y);
  lv_obj_set_style_text_font(object, font, 0);
  lv_obj_set_style_text_color(object, lv_color_hex(color), 0);
  lv_obj_set_style_text_align(object, align, 0);
  lv_obj_remove_flag(object, LV_OBJ_FLAG_CLICKABLE);
  return object;
}
void line(lv_layer_t *layer, int x1, int y1, int x2, int y2, uint32_t color, int width = 2) {
  lv_draw_line_dsc_t dsc;
  lv_draw_line_dsc_init(&dsc);
  dsc.p1 = {x1, y1}; dsc.p2 = {x2, y2}; dsc.width = width;
  dsc.round_start = dsc.round_end = true;
  dsc.color = lv_color_hex(color);
  lv_draw_line(layer, &dsc);
}
void disc(lv_layer_t *layer, int x, int y, int radius, uint32_t color) {
  lv_draw_rect_dsc_t dsc;
  lv_draw_rect_dsc_init(&dsc);
  dsc.radius = LV_RADIUS_CIRCLE;
  dsc.bg_color = lv_color_hex(color); dsc.bg_opa = LV_OPA_COVER;
  lv_area_t box = {x - radius, y - radius, x + radius, y + radius};
  lv_draw_rect(layer, &dsc, &box);
}
void draw_weather(lv_event_t *event) {
  auto *object = lv_event_get_target_obj(event);
  auto *layer = lv_event_get_layer(event);
  lv_area_t box; lv_obj_get_coords(object, &box);
  const int code = *static_cast<const int *>(lv_event_get_user_data(event));
  const int x = box.x1, y = box.y1;
  if (code < 0) {
    // Unknown weather has no invented sunny icon.
    line(layer, x+12, y+25, x+34, y+25, 0x8E8E93, 3);
  } else if ((code >= 71 && code <= 77) || code == 85 || code == 86) {
    for (int angle = 0; angle < 6; ++angle) {
      const float a = angle * 3.14159265f / 3;
      const int dx = static_cast<int>(std::lround(std::cos(a) * 21));
      const int dy = static_cast<int>(std::lround(std::sin(a) * 21));
      line(layer, x+23, y+24, x+23+dx, y+24+dy, 0xC2EEFF, 3);
      const int bx = x+23+dx*2/3, by = y+24+dy*2/3;
      for (const float side : {-0.8f, 0.8f})
        line(layer, bx, by, bx+static_cast<int>(std::cos(a+side)*7),
             by+static_cast<int>(std::sin(a+side)*7), 0xC2EEFF, 2);
    }
  } else {
    if (code <= 2) {
      const int cx = x+19, cy = y+17;
      for (int angle = 0; angle < 8; ++angle) {
        const float a = angle * 3.14159265f / 4;
        line(layer, cx+static_cast<int>(std::cos(a)*13), cy+static_cast<int>(std::sin(a)*13),
             cx+static_cast<int>(std::cos(a)*18), cy+static_cast<int>(std::sin(a)*18), kYellow);
      }
      disc(layer, cx, cy, 9, kYellow);
    }
    if (code != 0) {
      disc(layer, x+14, y+29, 9, kCyan);
      disc(layer, x+26, y+23, 12, kCyan);
      disc(layer, x+35, y+31, 8, kCyan);
      line(layer, x+12, y+35, x+35, y+35, kCyan, 7);
      if (code >= 51) {
        for (int i = 0; i < 3; ++i)
          line(layer, x+13+i*10, y+42, x+10+i*10, y+47, 0xB8E9FF);
      } else if (code == 45 || code == 48) {
        line(layer, x+7, y+43, x+39, y+43, 0xB8E9FF);
        line(layer, x+12, y+48, x+34, y+48, 0xB8E9FF);
      }
    }
  }
}
void draw_metrics(lv_event_t *event) {
  auto *layer = lv_event_get_layer(event);
  lv_area_t box; lv_obj_get_coords(lv_event_get_target_obj(event), &box);
  const int x = box.x1, y = box.y1;
  line(layer, x+5, y+3, x+5, y+16, kWhite, 6);
  disc(layer, x+5, y+18, 5, kWhite);
  line(layer, x+5, y+4, x+5, y+17, 0xFF6864, 2);
  disc(layer, x+5, y+18, 3, 0xFF6864);
  line(layer, x+5, y+33, x+1, y+42, kCyan, 3);
  line(layer, x+5, y+33, x+9, y+42, kCyan, 3);
  disc(layer, x+5, y+44, 5, kCyan);
  disc(layer, x+5, y+44, 2, 0x000000);
}
lv_obj_t *bar(lv_obj_t *parent, int y, uint32_t color) {
  auto *object = lv_bar_create(parent);
  lv_obj_remove_style_all(object);
  lv_obj_set_pos(object, 31, y); lv_obj_set_size(object, 48, 7);
  lv_obj_set_style_radius(object, 4, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(object, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(object, lv_color_hex(0x18222C), LV_PART_MAIN);
  lv_obj_set_style_border_color(object, lv_color_hex(0xE8F0F6), LV_PART_MAIN);
  lv_obj_set_style_border_width(object, 1, LV_PART_MAIN);
  lv_obj_set_style_pad_all(object, 2, LV_PART_MAIN);
  lv_obj_set_style_radius(object, 2, LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(object, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(object, lv_color_hex(color), LV_PART_INDICATOR);
  lv_bar_set_range(object, 0, 100);
  lv_bar_set_value(object, 0, LV_ANIM_OFF);
  lv_obj_remove_flag(object, LV_OBJ_FLAG_CLICKABLE);
  return object;
}
}

HomeContent home_content(const std::tm *local,
                         const product::WeatherSnapshot &weather,
                         bool background_missing) {
  HomeContent value;
  value.time = "--:--"; value.seconds = "--";
  value.date = "--月--日"; value.weekday = "未校时";
  if (local) {
    static constexpr const char *days[] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};
    char buffer[48];
    std::strftime(buffer, sizeof(buffer), "%H:%M", local); value.time = buffer;
    std::strftime(buffer, sizeof(buffer), "%S", local); value.seconds = buffer;
    std::snprintf(buffer, sizeof(buffer), "%d月%d日", local->tm_mon + 1, local->tm_mday);
    value.date = buffer; value.weekday = days[(local->tm_wday + 7) % 7];
  }
  value.place = weather.config.place.empty() ? "设置天气地点" : weather.config.place;
  value.temperature = "--°C"; value.humidity = "--%";
  if (weather.has_reading && weather.config.enabled) {
    value.temperature = std::to_string(static_cast<int>(std::lround(weather.reading.temperature_c))) + "°C";
    value.humidity = std::to_string(weather.reading.humidity_percent) + "%";
    value.weather_code = weather.reading.code;
    value.temperature_progress = std::clamp(static_cast<int>(std::lround((weather.reading.temperature_c + 20) * 100 / 70)), 0, 100);
    value.humidity_progress = weather.reading.humidity_percent;
  } else {
    if (!weather.config.enabled && weather.status != "error") value.status = "请在网页设置天气";
    else if (weather.status == "time_untrusted") value.status = "校时后更新天气";
    else if (weather.status == "offline") value.status = "网络未连接";
    else if (weather.status == "error") value.status = "天气暂不可用";
    else value.status = "正在更新天气";
  }
  if (background_missing) value.status = "背景不可用，请重新选择";
  return value;
}

void SpeakerHome::create(lv_obj_t *parent, const lv_font_t *clock_font,
                        const lv_font_t *seconds_font,
                        const lv_font_t *body_font, const lv_font_t *tiny_font,
                        const lv_font_t *heading_font) {
  root_ = lv_obj_create(parent);
  lv_obj_remove_style_all(root_);
  lv_obj_set_size(root_, 240, 240);
  lv_obj_remove_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(root_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_color(root_, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
  lv_obj_add_event_cb(root_, draw_boot_curtain, LV_EVENT_DRAW_MAIN, this);
  clock_height_ = lv_font_get_line_height(clock_font);
  seconds_height_ = lv_font_get_line_height(seconds_font);
  header_clip_ = clip(root_, 0, 0, 240, 64);
  clock_clip_ = clip(root_, 8, 56, 172, clock_height_);
  seconds_clip_ = clip(root_, 185, 97, 48, seconds_height_ + 1);
  date_clip_ = clip(root_, 0, 137, 240, 24);
  metrics_clip_ = clip(root_, 0, 166, 240, 66);
  place_ = label(header_clip_, 12, 9, 160, heading_font, kWhite);
  status_ = label(header_clip_, 12, 35, 161, tiny_font, 0xD4DFE8);
  icon_ = lv_obj_create(header_clip_);
  lv_obj_remove_style_all(icon_);
  lv_obj_set_pos(icon_, 184, 7); lv_obj_set_size(icon_, 48, 48);
  lv_obj_remove_flag(icon_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(icon_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(icon_, draw_weather, LV_EVENT_DRAW_MAIN, &weather_code_);
  hours_ = label(clock_clip_, 0, 0, 86, clock_font, kWhite);
  minutes_ = label(clock_clip_, 86, 0, 86, clock_font, kYellow);
  seconds_ = label(seconds_clip_, 0, 1, 48, seconds_font, kYellow, LV_TEXT_ALIGN_CENTER);
  date_ = label(date_clip_, 12, 0, 112, body_font, kWhite);
  weekday_ = label(date_clip_, 126, 0, 34, body_font, kGreen);
  temperature_ = label(metrics_clip_, 88, 0, 72, heading_font, kWhite);
  humidity_ = label(metrics_clip_, 88, 30, 72, heading_font, kWhite);
  temperature_bar_ = bar(metrics_clip_, 13, 0xFF6864);
  humidity_bar_ = bar(metrics_clip_, 43, kCyan);
  auto *metrics = lv_obj_create(metrics_clip_);
  lv_obj_remove_style_all(metrics);
  lv_obj_set_pos(metrics, 12, 0); lv_obj_set_size(metrics, 12, 51);
  lv_obj_remove_flag(metrics, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(metrics, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(metrics, draw_metrics, LV_EVENT_DRAW_MAIN, nullptr);
  hide();
}

void SpeakerHome::show(const HomeContent &content, bool has_background) {
  if (!root_) return;
  lv_obj_remove_flag(root_, LV_OBJ_FLAG_HIDDEN);
  // Only background alpha changes; children stay sharp, without a screen-size
  // compositing layer. The immutable wallpaper is owned by the display task.
  has_background_ = has_background;
  apply_boot_frame();
  text(hours_, content.time.substr(0, 2)); text(minutes_, content.time.substr(3, 2));
  text(seconds_, content.seconds); text(date_, content.date); text(weekday_, content.weekday);
  text(place_, content.place); text(status_, content.status);
  text(temperature_, content.temperature);
  text(humidity_, content.humidity);
  if (content.weather_code != weather_code_) {
    weather_code_ = content.weather_code;
    lv_obj_invalidate(icon_);
  }
  if (lv_bar_get_value(temperature_bar_) != content.temperature_progress)
    lv_bar_set_value(temperature_bar_, content.temperature_progress, LV_ANIM_OFF);
  if (lv_bar_get_value(humidity_bar_) != content.humidity_progress)
    lv_bar_set_value(humidity_bar_, content.humidity_progress, LV_ANIM_OFF);
}
void SpeakerHome::set_boot_frame(uint32_t elapsed_ms, int32_t settle_ms) {
  boot_frame_ = true;
  boot_elapsed_ms_ = elapsed_ms;
  boot_settle_ms_ = settle_ms;
  apply_boot_frame();
}
void SpeakerHome::finish_boot() {
  boot_frame_ = false;
  apply_boot_frame();
}
void SpeakerHome::apply_boot_frame() {
  if (!root_) return;
  const uint32_t settle = static_cast<uint32_t>(std::max<int32_t>(0, boot_settle_ms_));
  const float move = boot_frame_ ? ease(settle, 0, 760) : 1.0f;
  const int dx = static_cast<int>(std::lround(26 * (1.0f - move)));
  const int dy = static_cast<int>(std::lround(20 * (1.0f - move)));
  lv_obj_set_pos(clock_clip_, 8 + dx, 56 + dy);
  const float opening = boot_frame_ ? ease(boot_elapsed_ms_, 380, 660) : 1.0f;
  const int text_y = static_cast<int>(std::lround(clock_height_ * (1.0f - opening)));
  lv_obj_set_y(hours_, text_y);
  lv_obj_set_y(minutes_, text_y);
  // Reveal through small clipping containers: no full-screen alpha layer,
  // snapshots, duplicate clocks, or resampling of the actual home digits.
  const auto stage = [&](uint32_t delay, uint32_t duration) {
    return boot_frame_ ? ease(settle, delay, duration) : 1.0f;
  };
  reveal(seconds_clip_, seconds_height_ + 1, stage(300, 450));
  reveal(date_clip_, 24, stage(470, 450));
  reveal(header_clip_, 76, stage(620, 480));
  reveal(metrics_clip_, 66, stage(800, 500));
  const float background = stage(650, 750);
  const lv_opa_t opacity = has_background_ ? LV_OPA_60 : LV_OPA_COVER;
  // LVGL style setters invalidate even when the value has not changed.
  // During boot this is called on every display poll; keep idle frames clean.
  if (lv_obj_get_style_bg_opa(root_, LV_PART_MAIN) != opacity)
    lv_obj_set_style_bg_opa(root_, opacity, 0);
  // A spatial curtain redraws only the newly exposed band. Changing the
  // whole root's background alpha would blend the complete wallpaper on
  // every frame, exceeding this SPI display's frame budget.
  const int curtain = static_cast<int>(std::lround(256 * background));
  if (curtain != curtain_top_) {
    if (has_background_) {
      lv_area_t changed;
      lv_obj_get_coords(root_, &changed);
      changed.y2 = changed.y1 + std::min(239, std::max(curtain, curtain_top_) - 1);
      changed.y1 += std::max(0, std::min(curtain, curtain_top_) - 16);
      if (changed.y1 <= changed.y2) lv_obj_invalidate_area(root_, &changed);
    }
    curtain_top_ = curtain;
  }
}
void SpeakerHome::draw_boot_curtain(lv_event_t *event) {
  auto *view = static_cast<SpeakerHome *>(lv_event_get_user_data(event));
  if (!view->boot_frame_ || !view->has_background_ || view->curtain_top_ >= 256) return;
  lv_area_t bounds;
  lv_obj_get_coords(view->root_, &bounds);
  auto *layer = lv_event_get_layer(event);
  lv_draw_rect_dsc_t dsc;
  lv_draw_rect_dsc_init(&dsc);
  dsc.bg_color = lv_color_hex(0x000000);
  const int top = view->curtain_top_;
  for (int row = 0; row < 16; ++row) {
    const int y = bounds.y1 + top - 16 + row;
    if (y < bounds.y1 || y > bounds.y2) continue;
    dsc.bg_opa = static_cast<lv_opa_t>((row + 1) * 255 / 16);
    const lv_area_t band = {bounds.x1, y, bounds.x2, y};
    lv_draw_rect(layer, &dsc, &band);
  }
  if (top < 240) {
    bounds.y1 += top;
    dsc.bg_opa = LV_OPA_COVER;
    lv_draw_rect(layer, &dsc, &bounds);
  }
}
void SpeakerHome::hide() {
  if (root_) lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
}
} // namespace smart_speaker
