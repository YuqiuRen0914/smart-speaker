// Product opening choreography: a restrained line/title reveal inspired by
// X-TRACK's MIT StartUpView, and staged home entry studied in LVGL Music Demo.
// Original implementation; no upstream artwork, logo, or source is embedded.
#include "smart_speaker_boot.h"
#include "smart_speaker_home.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace smart_speaker {
namespace {
constexpr uint32_t kMinimumMs = 1500, kNetworkWaitMs = 4500, kSettleMs = 1400;
float ease(uint32_t elapsed, uint32_t duration) {
  const float t = std::min(1.0f, static_cast<float>(elapsed) / duration);
  return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
}
lv_obj_t *label(lv_obj_t *parent, int y, uint32_t color) {
  auto *object = lv_label_create(parent);
  lv_label_set_text(object, "");
  lv_obj_set_pos(object, 12, y);
  lv_obj_set_size(object, 216, 20);
  lv_label_set_long_mode(object, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_font(object, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_align(object, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(object, lv_color_hex(color), 0);
  lv_obj_remove_flag(object, LV_OBJ_FLAG_CLICKABLE);
  return object;
}
}

void SpeakerBootScreen::create(lv_obj_t *parent, SpeakerHome &home,
                               const char *version, uint32_t now_ms) {
  dismiss();
  home_ = &home;
  started_ms_ = now_ms;
  finishing_ = false;
  root_ = lv_obj_create(parent);
  lv_obj_remove_style_all(root_);
  lv_obj_set_size(root_, 240, 240);
  lv_obj_set_style_bg_color(root_, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
  lv_obj_remove_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(root_, LV_OBJ_FLAG_CLICKABLE);

  line_ = lv_obj_create(root_);
  lv_obj_remove_style_all(line_);
  lv_obj_set_pos(line_, 120, 162);
  lv_obj_set_size(line_, 0, 2);
  lv_obj_set_style_radius(line_, 1, 0);
  lv_obj_set_style_bg_color(line_, lv_color_hex(0xFFD426), 0);
  lv_obj_set_style_bg_opa(line_, LV_OPA_COVER, 0);
  lv_obj_remove_flag(line_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(line_, LV_OBJ_FLAG_CLICKABLE);
  status_ = label(root_, 185, 0xA7AFB8);
  version_ = label(root_, 214, 0x606B76);
  char text[48];
  std::snprintf(text, sizeof(text), "V%s", version ? version : "--");
  lv_label_set_text(version_, text);
}

bool SpeakerBootScreen::update(uint32_t now_ms, const BootNetworkStatus &network,
                               bool home_target) {
  if (!root_) return false;
  const uint32_t elapsed = now_ms - started_ms_;
  const bool ready = network.connected && network.clock_valid;
  const bool expired = elapsed >= kNetworkWaitMs;
  if (!finishing_ && elapsed >= kMinimumMs && (ready || network.needs_setup || expired)) {
    finishing_ = true;
    finishing_ms_ = now_ms;
  }
  const uint32_t settle = finishing_ ? now_ms - finishing_ms_ : 0;
  if (finishing_ && settle >= kSettleMs) {
    dismiss();
    return false;
  }
  if (home_target) home_->set_boot_frame(elapsed, finishing_ ? static_cast<int32_t>(settle) : -1);
  else home_->finish_boot();

  // The real home supplies the black opening background and moving digits.
  // Other saved desktops/provisioning retain a bounded black-cover fallback.
  const auto foreground = static_cast<lv_opa_t>(std::lround(255 * (1.0f - ease(settle, 260))));
  if (lv_obj_get_style_text_opa(status_, LV_PART_MAIN) != foreground)
    lv_obj_set_style_text_opa(status_, foreground, 0);
  if (lv_obj_get_style_text_opa(version_, LV_PART_MAIN) != foreground)
    lv_obj_set_style_text_opa(version_, foreground, 0);
  const float departure = ease(settle, 620);
  const int width = static_cast<int>(std::lround(172 * ease(elapsed, 420) * (1.0f - departure)));
  lv_obj_set_width(line_, width);
  lv_obj_set_x(line_, (240 - width) / 2);
  lv_obj_set_y(line_, 162 - static_cast<int>(std::lround(18 * departure)));
  const auto line_opacity = static_cast<lv_opa_t>(std::lround(255 * (1.0f - departure)));
  if (lv_obj_get_style_bg_opa(line_, LV_PART_MAIN) != line_opacity)
    lv_obj_set_style_bg_opa(line_, line_opacity, 0);
  const float fallback = finishing_ ? ease(settle > 900 ? settle - 900 : 0, 500) : 0;
  const auto cover_opacity = home_target ? static_cast<lv_opa_t>(LV_OPA_TRANSP) :
      static_cast<lv_opa_t>(std::lround(255 * (1.0f - fallback)));
  if (lv_obj_get_style_bg_opa(root_, LV_PART_MAIN) != cover_opacity)
    lv_obj_set_style_bg_opa(root_, cover_opacity, 0);

  const char *message = ready ? "Ready" :
      network.needs_setup ? "Wi-Fi setup required" :
      network.connected ? (expired ? "Time sync pending" : "Syncing time...") :
      expired ? "Offline - local mode" : "Connecting to Wi-Fi...";
  if (std::strcmp(lv_label_get_text(status_), message)) lv_label_set_text(status_, message);
  return true;
}

void SpeakerBootScreen::dismiss() {
  if (home_) home_->finish_boot();
  if (root_) lv_obj_delete(root_);
  home_ = nullptr;
  root_ = line_ = status_ = version_ = nullptr;
  finishing_ = false;
}
} // namespace smart_speaker
