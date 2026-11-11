#pragma once

#include <lvgl.h>

namespace smart_speaker {

class AppLauncher {
 public:
  void create(lv_obj_t *parent, const lv_font_t *heading, const lv_font_t *small);
  void show(unsigned index);
  void hide();

 private:
  static void draw_icon(lv_event_t *event);
  lv_obj_t *root_ = nullptr, *icon_ = nullptr, *title_ = nullptr;
  lv_obj_t *dots_[7]{};
  unsigned index_ = 7;
};

} // namespace smart_speaker
