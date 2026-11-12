#pragma once

#include <lvgl.h>
#include "product/product_state.h"

namespace smart_speaker {

class AppLauncher {
 public:
  void create(lv_obj_t *parent, const lv_font_t *heading, const lv_font_t *small);
  void show(unsigned index);
  void show_secondary(product::Module module, unsigned item);
  void hide();

 private:
  static void draw_icon(lv_event_t *event);
  lv_obj_t *root_ = nullptr, *icon_ = nullptr, *title_ = nullptr;
  lv_obj_t *section_ = nullptr, *secondary_icon_ = nullptr;
  lv_obj_t *dots_[6]{};
  unsigned index_ = 6;
  bool secondary_ = false;
};

} // namespace smart_speaker
