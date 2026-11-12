#pragma once
#include <lvgl.h>
#include "smart_speaker_ambient.h"
#include "product/desktop_resources.h"
namespace smart_speaker {
class AmbientView {
 public:
  void create(lv_obj_t *parent, const lv_font_t *font, const lv_font_t *small);
  void show(const ambient::Session &session, bool playing, bool paused, int volume,
            const std::string &error, const product::AmbientWallpaper &wallpaper, uint32_t now);
  void hide();
 private:
  lv_obj_t *root_=nullptr,*image_=nullptr,*overlay_=nullptr,*title_=nullptr,*status_=nullptr,*timer_=nullptr;
  lv_obj_t *choices_=nullptr;
  lv_image_dsc_t descriptor_{};
  std::shared_ptr<const product::WallpaperPixels> pixels_;
  const uint16_t *source_=nullptr;
  bool visible_=false, last_playing_=false, last_paused_=false;
  int last_volume_=-1;
  uint32_t changed_at_=0;
};
}
