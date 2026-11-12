#include "smart_speaker_wood_fish.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

extern "C" const lv_image_dsc_t speaker_wood_fish;

namespace smart_speaker {
namespace {
lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, int y, uint32_t color) {
  auto *o = lv_label_create(parent);
  lv_obj_set_width(o, 224);
  lv_obj_set_pos(o, 8, y);
  lv_obj_set_style_text_font(o, font, 0);
  lv_obj_set_style_text_align(o, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(o, lv_color_hex(color), 0);
  return o;
}
}
void WoodFishView::create(lv_obj_t *parent, const lv_font_t *heading, const lv_font_t *small) {
  root_ = lv_obj_create(parent);
  lv_obj_remove_style_all(root_);
  lv_obj_set_size(root_, 240, 240);
  lv_obj_remove_flag(root_, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
  lv_obj_set_style_bg_color(root_, lv_color_hex(0x171D20), 0);
  lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
  lv_label_set_text(label(root_, heading, 13, 0xD8D8D5), "电子木鱼");
  count_label_ = label(root_, small, 217, 0xA3ABAE);
  art_ = lv_obj_create(root_);
  lv_obj_remove_style_all(art_);
  lv_obj_remove_flag(art_, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
  lv_obj_set_pos(art_, 12, 83);
  lv_obj_set_size(art_, 216, 137);
  lv_obj_add_event_cb(art_, draw, LV_EVENT_DRAW_MAIN, this);
  body_ = lv_image_create(root_);
  lv_image_set_src(body_, &speaker_wood_fish);
  lv_obj_set_pos(body_, 30, 91);
  lv_obj_set_style_image_recolor(body_, lv_color_hex(0xD8D8D5), 0);
  lv_obj_set_style_image_recolor_opa(body_, LV_OPA_COVER, 0);
  lv_image_set_pivot(body_, 80, 124);
  // Dedicated clipped strip above both the body and the raised mallet.
  auto *gain_layer = lv_obj_create(root_);
  lv_obj_remove_style_all(gain_layer);
  lv_obj_set_pos(gain_layer, 0, 40);
  lv_obj_set_size(gain_layer, 240, 49);
  lv_obj_remove_flag(gain_layer, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
  for (auto &gain : gains_) {
    gain.label = label(gain_layer, heading, 24, 0xD8D8D5);
    lv_obj_set_width(gain.label, 48);
    lv_label_set_text(gain.label, "+1");
    lv_obj_add_flag(gain.label, LV_OBJ_FLAG_HIDDEN);
  }
  lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
}
void WoodFishView::show(uint32_t count, uint32_t now) {
  if (!root_) return;
  if (!visible_ || count != count_) {
    char text[48];
    std::snprintf(text, sizeof(text), "功德  %lu", static_cast<unsigned long>(count));
    lv_label_set_text(count_label_, text);
    if (visible_) {
      const uint32_t delta = count - count_;
      // Independent particles: fast taps never restart a previous +1. A fixed
      // pool bounds burst work and memory; total count remains authoritative.
      constexpr int offsets[] = {-45, 0, 45, -30, 15, -15, 30, 0};
      for (uint32_t i = 0; i < std::min(delta, uint32_t(gains_.size())); ++i) {
        auto &gain = gains_[next_gain_];
        gain.started = now;
        gain.active = true;
        lv_obj_set_x(gain.label, 96 + offsets[next_gain_]);
        next_gain_ = (next_gain_ + 1) % gains_.size();
      }
      if (age_ >= 65) { started_ = now; age_ = 0; }
    } else {
      lv_image_set_scale(body_, 256);
      age_ = 700; // Opening/resuming a page must never replay an old hit.
      for (auto &gain : gains_) { gain.active = false; lv_obj_add_flag(gain.label, LV_OBJ_FLAG_HIDDEN); }
      next_gain_ = 0;
      lv_obj_remove_flag(root_, LV_OBJ_FLAG_HIDDEN);
      lv_obj_move_foreground(root_);
    }
    count_ = count;
    visible_ = true;
    lv_obj_invalidate(art_);
  }
  if (age_ < 700) {
    age_ = std::min(uint32_t(now - started_), uint32_t(700));
    lv_obj_invalidate(art_);
    const float hit = age_ < 65 ? 0 : age_ < 135 ? float(age_ - 65) / 70 :
        age_ < 300 ? 1.0f - float(age_ - 135) / 165 : 0;
    lv_image_set_scale(body_, 256 - static_cast<int>(hit * 16));
  }
  for (auto &gain : gains_) {
    if (!gain.active) continue;
    const uint32_t elapsed = now - gain.started;
    if (elapsed >= 760) {
      gain.active = false;
      lv_obj_add_flag(gain.label, LV_OBJ_FLAG_HIDDEN);
      continue;
    }
    lv_obj_set_y(gain.label, 24 - static_cast<int>(elapsed * 24 / 760));
    const auto opacity = elapsed < 200 ? 255 : 255 * (760 - elapsed) / 560;
    lv_obj_set_style_text_opa(gain.label, static_cast<lv_opa_t>(opacity), 0);
    lv_obj_remove_flag(gain.label, LV_OBJ_FLAG_HIDDEN);
  }
}
void WoodFishView::hide() {
  if (!root_ || !visible_) return;
  visible_ = false;
  age_ = 700;
  lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
  for (auto &gain : gains_) { gain.active = false; lv_obj_add_flag(gain.label, LV_OBJ_FLAG_HIDDEN); }
}
void WoodFishView::draw(lv_event_t *event) {
  const auto &v = *static_cast<WoodFishView *>(lv_event_get_user_data(event));
  lv_area_t box;
  lv_obj_get_coords(v.art_, &box);
  auto *layer = lv_event_get_layer(event);
  const auto rect = [&](int x, int y, int w, int h, uint32_t color, int radius,
                        int border = 0, int opacity = 255) {
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.radius = radius;
    d.bg_color = d.border_color = lv_color_hex(color);
    d.bg_opa = border ? LV_OPA_TRANSP : static_cast<lv_opa_t>(opacity);
    d.border_width = border;
    d.border_opa = static_cast<lv_opa_t>(opacity);
    const lv_area_t area{box.x1 + x, box.y1 + y, box.x1 + x + w - 1, box.y1 + y + h - 1};
    lv_draw_rect(layer, &d, &area);
  };
  const auto line = [&](int x1, int y1, int x2, int y2, int width, uint32_t color) {
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.p1 = {box.x1 + x1, box.y1 + y1}; d.p2 = {box.x1 + x2, box.y1 + y2};
    d.width = width; d.color = lv_color_hex(color);
    d.round_start = d.round_end = true;
    lv_draw_line(layer, &d);
  };
  // Monochrome silhouette and a separate round-headed mallet, matching the
  // supplied reference. Geometry remains inside the existing local art area.
  const float t = static_cast<float>(v.age_);
  const float drop = t < 65 ? t / 65 : t < 240 ? 1 - (t - 65) / 175 : 0;
  const int head_y = 8 + static_cast<int>(drop * 21);
  line(200, 58, 119, head_y + 12, 10, 0xD8D8D5);
  rect(101, head_y, 33, 33, 0xD8D8D5, 17);
}
}
