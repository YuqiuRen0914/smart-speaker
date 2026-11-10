#pragma once
#include "lvgl.h"
// Explicit layout selected by each App. Creators mark fixed chrome with
// LV_OBJ_FLAG_FLOATING; other children form a column of cards. Membership
// never depends on the coordinates or widths of another viewport layout.
static inline bool lunaros_view_is_compact(void) {
  return lv_display_get_horizontal_resolution(NULL) < 360;
}
static inline void lunaros_view_column_card(lv_obj_t *obj, int width) {
  if(lv_obj_check_type(obj, &lv_label_class)) {
    lv_obj_set_width(obj, width);
    lv_label_set_long_mode(obj, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_height(obj, LV_SIZE_CONTENT);
    return;
  }
  if(lv_obj_check_type(obj, &lv_keyboard_class)) {
    lv_obj_set_width(obj, width);
    lv_obj_set_height(obj, 120);
    return;
  }
  lv_obj_set_style_max_width(obj, width, 0);
  if(lv_obj_get_child_count(obj) == 0) return; // intrinsic canvas/image/slider
  lv_obj_set_width(obj, width);
  if(lv_obj_check_type(obj, &lv_textarea_class)) return;
  lv_obj_set_layout(obj, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(obj, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(obj, 8, 0);
  lv_obj_set_style_pad_row(obj, 6, 0);
  lv_obj_set_height(obj, LV_SIZE_CONTENT);
  lv_obj_set_style_min_height(obj, 44, 0);
  lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
  for(uint32_t i = 0; i < lv_obj_get_child_count(obj); ++i) {
    lv_obj_t *child = lv_obj_get_child(obj, i);
    if(lv_obj_has_flag(child, LV_OBJ_FLAG_HIDDEN) || lv_obj_has_flag(child, LV_OBJ_FLAG_FLOATING)) continue;
    lunaros_view_column_card(child, width - 16);
    lv_obj_set_style_align(child, LV_ALIGN_DEFAULT, 0);
    lv_obj_set_pos(child, 0, 0);
  }
}
// Fixed chrome stays in root. The separate body clips scrolling content below
// the header; FLOATING alone would let cards paint/hit-test over the controls.
static inline void lunaros_view_column_page(lv_obj_t *root, int content_top) {
  if(!lunaros_view_is_compact()) return;
  lv_obj_update_layout(root);
  lv_obj_t *body = lv_obj_create(root);
  lv_obj_remove_style_all(body);
  lv_obj_set_pos(body, 0, content_top);
  lv_obj_set_size(body, lv_obj_get_width(root), lv_obj_get_height(root) - content_top);
  lv_obj_set_scroll_dir(body, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_set_style_pad_bottom(body, 12, 0);
  lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
  const int width = lv_obj_get_width(root) - 24;
  int y = 0;
  uint32_t i = 0;
  while(i < lv_obj_get_child_count(root)) {
    lv_obj_t *child = lv_obj_get_child(root, i);
    if(child == body) break;
    if(lv_obj_has_flag(child, LV_OBJ_FLAG_FLOATING)) { ++i; continue; }
    lv_obj_set_parent(child, body);
    if(lv_obj_has_flag(child, LV_OBJ_FLAG_HIDDEN)) continue;
    lunaros_view_column_card(child, width);
    lv_obj_set_style_align(child, LV_ALIGN_DEFAULT, 0);
    lv_obj_set_pos(child, 12, y);
    lv_obj_update_layout(child);
    y += lv_obj_get_height(child) + 10;
  }
}
