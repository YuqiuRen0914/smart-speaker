#include "lunaredu_pet_desktop_surface.h"
#include "lunaredu_pet_interaction.h"
#include "lunaredu_pet_ui.h"
#include "lunaredu_pet_render.h"
#include <esp_timer.h>

namespace lunaredu::pet {
void DesktopSceneSurface::clear(){std::lock_guard<std::mutex> lock(presentation_mutex_);activation_.fetch_add(1,std::memory_order_release);latest_.reset();touch_down_=false;completion_.clear();presented_revision_.store(0,std::memory_order_release);}
DesktopSceneStatus DesktopSceneSurface::status() const{const auto s=scene_status();return {s.published,s.pool_busy,raster_us_.load()};}
void DesktopSceneSurface::paint(const Scene &scene,uint64_t activation,desktop::UiPaintPort &port){
  if(activation!=activation_.load(std::memory_order_acquire)||!side_||!scene||!port.begin_region(side_,side_,0,0,false))return;
  const auto started=esp_timer_get_time();const auto target=port.retained_surface(this,activation);const auto surface=target.canvas;FramePresentation frame;
  const auto changed=target.overwritten;
  if(!paint_scene(scene,{surface.pixels,surface.width,surface.height,surface.stride,0,surface.antialias?1:0,1},frame,
      {target.retained,{changed.x,changed.y,changed.width,changed.height}}))return;
  raster_us_.store(uint32_t(esp_timer_get_time()-started));
  port.rotate(port.rotation());
  display::FrameDamageMap candidates(side_,side_);
  if(baseline_.valid&&frame.content_signature&&baseline_.signature==frame.content_signature&&baseline_.rotation==port.rotation()){
    const auto mark=[&](display::Damage area){
      for(unsigned r=0;r<(port.rotation()&3);++r){const int x=area.x;area.x=side_-area.y-area.height;area.y=x;std::swap(area.width,area.height);}
      candidates.mark(area);
    };
    mark(baseline_.visible);mark({frame.visible.x,frame.visible.y,frame.visible.width,frame.visible.height});
  }else candidates.mark({0,0,side_,side_});
  // Body bounds are candidate coverage, not proof that every interior pixel
  // changed. Let Display compare them against its acknowledged baseline.
  port.damage_map(candidates);
  {
    std::lock_guard<std::mutex> lock(presentation_mutex_);
    if(activation!=activation_.load(std::memory_order_acquire))return;
    if(!completion_.prepare(port.submission_canvas(),frame.input_revision))presented_revision_.store(0,std::memory_order_release);
  }
  if(!port.flush_region(0,0)){baseline_.valid=false;presented_revision_.store(0,std::memory_order_release);}
  else baseline_={{frame.visible.x,frame.visible.y,frame.visible.width,frame.visible.height},frame.content_signature,port.rotation(),true};
}
desktop::UiRenderCache DesktopSceneSurface::cache(){return {&baseline_,[](void *p){*static_cast<PaintBaseline *>(p)={};}};}
void DesktopSceneSurface::presented(display::ScreenFrameView frame){std::lock_guard<std::mutex> lock(presentation_mutex_);const auto revision=completion_.complete(frame);if(revision)presented_revision_.store(revision,std::memory_order_release);}
void DesktopSceneSurface::unchanged(display::FrameCanvas canvas){std::lock_guard<std::mutex> lock(presentation_mutex_);const auto revision=completion_.complete(canvas);if(revision)presented_revision_.store(revision,std::memory_order_release);}
void DesktopSceneSurface::touch(bool system_owned,bool valid,bool pressed,uint16_t x,uint16_t y,unsigned rotation,uint32_t now){
  TouchResult result;
  if(system_owned||!valid){if(touch_down_)(void)handle_touch({TouchPhase::Cancel,0,0,side_,side_,now},result);touch_down_=false;return;}
  if(!side_)return;
  if(pressed&&(x>=side_||y>=side_)){
    if(touch_down_)(void)handle_touch({TouchPhase::Cancel,0,0,side_,side_,now},result);
    touch_down_=false;return;
  }
  const auto revision=presented_revision_.load(std::memory_order_acquire);
  if(!revision){touch_down_=false;return;}
  if(pressed){
    for(unsigned i=0;i<(rotation&3);++i){const auto old=x;x=y;y=uint16_t(side_-1-old);}
    touch_x_=x;touch_y_=y;
    (void)handle_touch({touch_down_?TouchPhase::Move:TouchPhase::Down,x,y,side_,side_,now,revision},result);touch_down_=true;
  }
  // Contact-free FT6236 samples contain no point (x/y are zero). Release
  // at the last valid logical point, as the shared desktop router does.
  else if(touch_down_){(void)handle_touch({TouchPhase::Up,touch_x_,touch_y_,side_,side_,now,revision},result);touch_down_=false;}
}
void DesktopScenePresentation::paint(desktop::UiPaintPort &port,bool) const{if(owner)owner->paint(scene,activation,port);}
desktop::UiRenderCache DesktopScenePresentation::render_cache() const{return owner?owner->cache():desktop::UiRenderCache{};}
}
