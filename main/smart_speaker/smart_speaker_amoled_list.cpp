/*
 * Adapted from AmoledOS aos_launcher.c, commit
 * 3e8a3deead60cf86b5994a770007ff3247b8fdf9: setup_list, add_list,
 * apply_falloff and list_scroll_cb. Copyright (c) 2026 Charlie Gallo.
 * SPDX-License-Identifier: MIT
 * Full license: components/lunaredu_character_assets/third_party/amoledos/LICENSE
 *
 * Product adaptation: 240x240 viewport, seven bounded reusable icon/text rows,
 * no app registry/HAL, folder state or input callbacks. Navigation and focus
 * remain in ProductState. Like upstream, fading affects children only and
 * only changes opacity when needed; rows never become compositing layers.
 */
#include "smart_speaker_amoled_list.h"

#include <algorithm>

namespace smart_speaker {
namespace {
constexpr int32_t kRowHeight = 48;

void apply_falloff(lv_obj_t *row, int32_t distance) {
  int32_t opacity = LV_OPA_COVER;
  if (distance > 0 && !lv_obj_has_state(row, LV_STATE_FOCUSED)) {
    const int32_t over = std::min(distance, kRowHeight + 20);
    opacity -= (LV_OPA_COVER - 40) * over / (kRowHeight + 20);
  }
  const uint32_t count = lv_obj_get_child_count(row);
  for (uint32_t i = 0; i < count; ++i) {
    auto *child = lv_obj_get_child(row, i);
    if (lv_obj_get_style_opa(child, LV_PART_MAIN) != static_cast<lv_opa_t>(opacity))
      lv_obj_set_style_opa(child, static_cast<lv_opa_t>(opacity), 0);
  }
}

void list_scroll_cb(lv_event_t *event) {
  auto *container = static_cast<lv_obj_t *>(lv_event_get_target(event));
  lv_area_t bounds;
  lv_obj_get_coords(container, &bounds);
  for (uint32_t i = 0; i < lv_obj_get_child_count(container); ++i) {
    auto *row = lv_obj_get_child(container, i);
    if (lv_obj_has_flag(row, LV_OBJ_FLAG_HIDDEN)) continue;
    lv_area_t area;
    lv_obj_get_coords(row, &area);
    const int32_t center = (area.y1 + area.y2) / 2;
    const int32_t distance = center < bounds.y1 + kRowHeight ? bounds.y1 + kRowHeight - center :
        center > bounds.y2 - kRowHeight ? center - (bounds.y2 - kRowHeight) : 0;
    apply_falloff(row, distance);
  }
}

void label_text(lv_obj_t *label, const char *text) {
  if (std::string(lv_label_get_text(label)) != text) lv_label_set_text(label, text);
}
}

void AmoledList::create(lv_obj_t *parent, const lv_font_t *font) {
  container_ = lv_obj_create(parent);
  lv_obj_remove_style_all(container_);
  lv_obj_set_pos(container_, 12, 46);
  lv_obj_set_size(container_, 216, 160);
  lv_obj_remove_flag(container_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(container_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(container_, 6, 0);
  lv_obj_set_style_pad_ver(container_, 4, 0);
  lv_obj_set_scroll_dir(container_, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(container_, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_event_cb(container_, list_scroll_cb, LV_EVENT_SCROLL, nullptr);
  for (auto &row : rows_) {
    row = lv_obj_create(container_);
    lv_obj_remove_style_all(row);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(row, 212, kRowHeight);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(row, 8, 0);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_set_style_radius(row, 10, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    auto *icon = lv_label_create(row);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_16, 0);
    lv_obj_set_width(icon, 24);
    lv_obj_set_style_text_align(icon, LV_TEXT_ALIGN_CENTER, 0);
    auto *label = lv_label_create(row);
    lv_obj_set_style_text_font(label, font, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(label, 164);
  }
  hide();
}

void AmoledList::show(const std::array<std::string, kMaxItems> &labels,
                      const std::array<const char *, kMaxItems> &icons,
                      size_t count, size_t selected, int page_key) {
  count = std::min(count, kMaxItems);
  if (!container_ || !count || selected >= count) { hide(); return; }
  const bool moved = page_key != page_key_ || selected != selected_ || lv_obj_has_flag(container_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(container_, LV_OBJ_FLAG_HIDDEN);
  for (size_t i = 0; i < rows_.size(); ++i) {
    auto *row = rows_[i];
    if (i >= count) { lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN); continue; }
    lv_obj_remove_flag(row, LV_OBJ_FLAG_HIDDEN);
    label_text(lv_obj_get_child(row, 0), icons[i] ? icons[i] : LV_SYMBOL_RIGHT);
    // LVGL's DOT mode edits its own text buffer. Compare the input label to
    // the previous input, otherwise long catalog names redraw every poll.
    if (row_labels_[i] != labels[i]) {
      row_labels_[i] = labels[i];
      lv_label_set_text(lv_obj_get_child(row, 1), row_labels_[i].c_str());
    }
    const bool focused = i == selected;
    if (focused != lv_obj_has_state(row, LV_STATE_FOCUSED) || page_key_ == -1) {
      if (focused) lv_obj_add_state(row, LV_STATE_FOCUSED); else lv_obj_remove_state(row, LV_STATE_FOCUSED);
      lv_obj_set_style_bg_color(row, lv_color_hex(focused ? 0x292313 : 0x0D0D0D), 0);
      lv_obj_set_style_text_color(row, lv_color_hex(focused ? 0xF4C95D : 0xE8E6DF), 0);
    }
  }
  if (moved) {
    lv_obj_update_layout(container_);
    lv_obj_scroll_to_view(rows_[selected], LV_ANIM_OFF);
    lv_obj_send_event(container_, LV_EVENT_SCROLL, nullptr);
  }
  page_key_ = page_key;
  selected_ = selected;
}

void AmoledList::hide() {
  if (container_) lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);
}
} // namespace smart_speaker
