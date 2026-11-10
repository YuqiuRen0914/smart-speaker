#include "desktop/ui/status_bar_cache.h"
#include "graphics/scene.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#ifndef LUNAROS_STATUS_HANDLE_CACHE
#define LUNAROS_STATUS_HANDLE_CACHE 1
#endif
#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#endif
namespace lunaredu::desktop {
namespace {
constexpr size_t band_bytes=size_t(kDesktopCanvas.width)*status_layout::height*sizeof(uint16_t);
void *allocate_band(size_t bytes){
#ifdef ESP_PLATFORM
  return heap_caps_malloc(bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
#else
  return std::malloc(bytes);
#endif
}
void free_band(void *data){
#ifdef ESP_PLATFORM
  heap_caps_free(data);
#else
  std::free(data);
#endif
}
template<size_t N> bool copy_text(std::array<char,N> &out,const char *value){
  if(!value)return true;
  for(size_t i=0;i<N;++i){out[i]=value[i];if(!value[i])return true;}
  return false;
}
}
StatusBarRasterCache::StatusBarRasterCache():memory_{allocate_band,free_band}{}
void StatusBarRasterCache::reset(){
  if(pixels_)memory_.release(pixels_);
  pixels_=nullptr;attempted_=valid_=hidden_=false;key_={};stats_={};
}
bool StatusBarRasterCache::Key::operator==(const Key &other)const{
  return title==other.title&&detail==other.detail&&font==other.font&&flags==other.flags&&battery==other.battery&&
      renderer==other.renderer;
}
bool StatusBarRasterCache::prepare(Key &key,const void *renderer,const char *title,const DesktopStatus &status,bool aa,const char *detail,uint32_t font){
  if(!font||!copy_text(key.title,status.network_title?status.location:title)||!copy_text(key.detail,detail))return false;
  key.renderer=renderer;key.font=font;key.battery=status.battery_percent;
  key.flags=unsigned(status.wifi)|(unsigned(status.wifi_connecting)<<1)|(unsigned(status.bluetooth)<<2)|
      (unsigned(status.platform)<<3)|(unsigned(status.peer)<<4)|(unsigned(status.charging)<<5)|
      (unsigned(status.peer_severity)<<6)|(unsigned(aa)<<8);
  return true;
}
bool StatusBarRasterCache::paint(lunaros::graphics::Scene &renderer,lunaros::graphics::CanvasSpec spec,
    const char *title,const DesktopStatus &status,uint16_t *pixels,bool aa,const char *detail,uint32_t font){
  if(!pixels||!spec.valid()||spec.width!=kDesktopCanvas.width||spec.stride!=kDesktopCanvas.stride||
      spec.height<status_layout::height||spec.height>renderer.canvas().height||spec.width>renderer.canvas().width)return false;
  Key next{};
  const bool candidate=band_bytes<=64*1024&&prepare(next,&renderer,title,status,aa,detail,font);
  if(candidate&&valid_&&!hidden_&&next==key_){std::memcpy(pixels,pixels_,band_bytes);++stats_.hits;return true;}
  valid_=false;
  // Cache only completed native draws. A failed frame is rejected by the host;
  // allocation failure merely bypasses caching after a successful draw.
  if(!paint_status_overlay(renderer,spec,pixels,title,status,{},false,aa,detail))return false;
  if(!candidate){++stats_.bypasses;return true;}
  if(!pixels_&&!attempted_){
    attempted_=true;
    if(memory_.allocate&&memory_.release)pixels_=static_cast<uint16_t *>(memory_.allocate(band_bytes));
  }
  if(!pixels_){++stats_.bypasses;return true;}
  std::memcpy(pixels_,pixels,band_bytes);key_=next;valid_=true;hidden_=false;stats_.bytes=band_bytes;++stats_.misses;
  return true;
}
bool StatusBarRasterCache::paint_hidden(lunaros::graphics::Scene &renderer,lunaros::graphics::CanvasSpec spec,uint16_t *pixels,bool aa){
  if constexpr(!LUNAROS_STATUS_HANDLE_CACHE)return paint_status_overlay(renderer,spec,pixels,"",{},{},true,aa);
  namespace m=lunaros::graphics::metrics;
  constexpr int width=m::px(40),height=m::px(3),x=(canvas_layout::side-width)/2,y=m::px(4);
  if(!pixels||!spec.valid()||spec.width!=kDesktopCanvas.width||spec.stride!=kDesktopCanvas.stride||spec.height<y+height||
      spec.height>renderer.canvas().height||spec.width>renderer.canvas().width)return false;
  Key next{};next.renderer=&renderer;next.flags=unsigned(aa);
  if(valid_&&hidden_&&next==key_){
    for(int row=0;row<height;++row)std::memcpy(pixels+size_t(y+row)*spec.stride+x,pixels_+row*width,width*2);
    ++stats_.hits;return true;
  }
  valid_=false;
  if(!paint_status_overlay(renderer,spec,pixels,"",{},{},true,aa))return false;
  if(!pixels_&&!attempted_){attempted_=true;if(memory_.allocate&&memory_.release)pixels_=static_cast<uint16_t *>(memory_.allocate(band_bytes));}
  if(!pixels_){++stats_.bypasses;return true;}
  // The handle is opaque. Store only its exact LVGL coverage; copying a full
  // status band would overwrite the application's changing background.
  for(int row=0;row<height;++row)std::memcpy(pixels_+row*width,pixels+size_t(y+row)*spec.stride+x,width*2);
  key_=next;valid_=hidden_=true;stats_.bytes=band_bytes;++stats_.misses;return true;
}
}
