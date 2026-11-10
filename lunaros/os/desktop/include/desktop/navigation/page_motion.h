#pragma once
#include "desktop/navigation/home_navigation_renderer.h"
#include "lunaros_ui_theme.h"
#include "graphics/color.h"
#include "graphics/animation.h"
#include "display/pipeline/pixel_orientation.h"
#include <algorithm>
#include <cstdint>

namespace lunaredu::desktop {
// Finite, renderer-owned motion. Buffers are borrowed from the existing four
// frame screen pipeline and never allocated by a transition. Input/lifecycle
// changes replace a transition immediately; ordinary same-page refreshes can
// wait for the finite launch preview to settle without freezing business state.
class PageMotion {
 public:
  bool select(uint64_t key,bool launcher,bool allowed,uint32_t now) {
    const bool changed=key!=key_||launcher!=launcher_;
    if(changed){opening_=launcher_&&!launcher;started_=now;prepared_=false;key_=key;launcher_=launcher;active_=allowed&&!launcher;}
    if(!allowed||launcher)active_=false;
    return changed;
  }
  bool tick(uint32_t now){
    if(!active_||!prepared_)return false;
    if(uint32_t(now-started_)<uint32_t(lunaros::graphics::design::pageEnterMs)&&uint32_t(now-last_frame_)<16)return false;
    last_frame_=now;
    if(uint32_t(now-started_)>=uint32_t(lunaros::graphics::design::pageEnterMs))active_=false;
    return true; // Includes the final untransformed redraw.
  }
  bool active() const{return active_;}
  uint32_t next_wake_ms(uint32_t now) const {
    if(!active_||!prepared_)return UINT32_MAX;
    const auto elapsed=uint32_t(now-last_frame_),total=uint32_t(now-started_);
    const auto duration=uint32_t(lunaros::graphics::design::pageEnterMs);
    return std::min(elapsed>=16?0u:16-elapsed,total>=duration?0u:duration-total);
  }
  bool opening() const{return opening_;}
  void prepared(uint32_t now){if(active_){if(!prepared_){started_=now;prepared_=true;}last_frame_=now;}}
  void finish(){active_=false;}
  float progress(uint32_t now) const {
    if(!active_)return 1.f;
    if(!prepared_)return 0.f;
    return lunaros::graphics::animation_progress(uint32_t(now-started_),lunaros::graphics::design::pageEnterMs);
  }
  bool compose(uint16_t *frame,const uint16_t *source,const uint16_t *background,
      HomeTarget target,unsigned rotation,bool aa,uint32_t now,lunaros::graphics::Scene *renderer=nullptr) const;

 private:
  uint64_t key_=0;
  uint32_t started_=0;
  uint32_t last_frame_=0;
  bool launcher_=true,opening_=false,active_=false,prepared_=false;
};
}
