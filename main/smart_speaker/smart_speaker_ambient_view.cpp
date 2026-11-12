#include "smart_speaker_ambient_view.h"
#include "src/misc/cache/instance/lv_image_cache.h"
extern "C" const uint16_t speaker_ambient_wallpapers[4][57600];
namespace smart_speaker {
namespace {
void show_object(lv_obj_t *object,bool visible) {
  if (visible) lv_obj_remove_flag(object,LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(object,LV_OBJ_FLAG_HIDDEN);
}
void text(lv_obj_t *label,const std::string &value) {
  if (value!=lv_label_get_text(label)) lv_label_set_text(label,value.c_str());
}
lv_obj_t *label(lv_obj_t *parent,const lv_font_t *font,int y,int height) {
  auto *o=lv_label_create(parent); lv_obj_set_pos(o,12,y); lv_obj_set_size(o,216,height);
  lv_obj_set_style_text_font(o,font,0); lv_obj_set_style_text_color(o,lv_color_hex(0xF4F1E9),0);
  lv_obj_set_style_text_align(o,LV_TEXT_ALIGN_CENTER,0); lv_label_set_long_mode(o,LV_LABEL_LONG_DOT); return o;
}
lv_obj_t *panel(lv_obj_t *parent,int y,int height) {
  auto *o=lv_obj_create(parent); lv_obj_remove_style_all(o); lv_obj_set_pos(o,0,y); lv_obj_set_size(o,240,height);
  lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE); lv_obj_set_style_bg_color(o,lv_color_hex(0x172023),0);
  lv_obj_set_style_bg_opa(o,LV_OPA_80,0); return o;
}
}
void AmbientView::create(lv_obj_t *parent,const lv_font_t *font,const lv_font_t *small) {
  root_=panel(parent,0,240); lv_obj_set_style_bg_opa(root_,LV_OPA_COVER,0);
  image_=lv_image_create(root_); lv_obj_set_pos(image_,0,0);
  overlay_=panel(root_,140,100);
  title_=label(overlay_,font,6,26); status_=label(overlay_,small,34,20); timer_=label(overlay_,font,58,30);
  choices_=panel(root_,0,240); label(choices_,font,20,30);
  text(lv_obj_get_child(choices_,0),"定时停止");
  constexpr const char *names[]={"持续播放","30 分钟","60 分钟","90 分钟"};
  for(int i=0;i<4;++i) text(label(choices_,font,68+i*40,28),names[i]);
  hide();
}
void AmbientView::show(const ambient::Session &s,bool playing,bool paused,int volume,
                       const std::string &error,const product::AmbientWallpaper &wallpaper,uint32_t now) {
  const int scene=s.shown_scene();
  const uint16_t *data=wallpaper.pixels ? wallpaper.pixels->data() : speaker_ambient_wallpapers[scene];
  if (data!=source_) {
    lv_image_set_src(image_,nullptr); lv_image_cache_drop(&descriptor_);
    pixels_=wallpaper.pixels; source_=data; descriptor_={};
    descriptor_.header.magic=LV_IMAGE_HEADER_MAGIC; descriptor_.header.cf=LV_COLOR_FORMAT_RGB565;
    descriptor_.header.w=descriptor_.header.h=240; descriptor_.header.stride=480;
    descriptor_.data_size=115200; descriptor_.data=reinterpret_cast<const uint8_t*>(data);
    lv_image_set_src(image_,&descriptor_); changed_at_=now;
  }
  if (!visible_ || playing!=last_playing_ || paused!=last_paused_ || volume!=last_volume_) changed_at_=now;
  last_playing_=playing; last_paused_=paused; last_volume_=volume;
  text(title_,ambient::kNames[scene]);
  text(status_,!error.empty() ? error : !wallpaper.error.empty() ? "壁纸失效，已用默认图" :
      s.choosing ? "左右选择 · 轻点播放" : (playing ? "正在播放" : paused ? "已暂停" : "已停止") + std::string(" · 音量 ") + std::to_string(volume));
  const auto minutes=(s.remaining(now)+59999)/60000;
  text(timer_,s.choosing ? std::to_string(scene+1)+" / 4" :
      s.timer_duration ? "定时 · 剩余 "+std::to_string(minutes)+" 分钟" : "定时 · 持续播放");
  show_object(overlay_,!s.timer_menu && (s.visible(now) || uint32_t(now-changed_at_)<3500 || !error.empty() || !wallpaper.error.empty()));
  show_object(choices_,s.timer_menu);
  show_object(root_,true); if(!visible_) lv_obj_move_foreground(root_); visible_=true;
}
void AmbientView::hide() { if(root_) show_object(root_,false); visible_=false; }
}
