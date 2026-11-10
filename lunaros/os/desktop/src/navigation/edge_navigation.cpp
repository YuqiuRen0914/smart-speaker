#include "desktop/navigation/edge_navigation.h"
#include "graphics/animation.h"
namespace lunaredu::desktop {
bool EdgeNavigation::pointer_impl(bool pressed,bool valid,float x,float py,uint32_t now,bool animations,const Geometry &g) {
    if(!enabled)return false;
    if(!valid||(pressed&&(!std::isfinite(x)||!std::isfinite(py)||x<0||x>=g.screen_size||py<0||py>=g.screen_size))){
      const bool consumed=owned;cancel(now,animations);return consumed;
    }
    if(blocked_){if(!pressed)blocked_=false;return false;}
    // Releasing one gesture cannot confirm a second action during its settle.
    if(committing_)return true;
    if(pressed&&!contact){
      // A new contact invalidates the previous rebound.
      moving=owned=ready=finished_=false;progress=0;
      contact=true;sx_=lx_=x;sy_=ly_=py;started_=now;
      right=x>=g.screen_size-g.zone;
      rejected_=py<top_inset||py>=g.screen_size-g.edgeHeight||
          (x>g.zone&&!right);
      y=std::clamp(py,float(g.min_y),float(g.max_y));
      return false;
    }
    if(!contact)return false;
    if(pressed){lx_=x;ly_=py;}
    const float inward=(lx_-sx_)*(right?-1.f:1.f),dy=ly_-sy_;
    if(pressed){
      if(rejected_)return false;
      if(!owned){
        if((std::abs(dy)>=g.deadzone&&std::abs(dy)>std::abs(inward))||inward<=-g.deadzone){rejected_=true;return false;}
        if(inward<g.deadzone||inward<std::abs(dy)*1.5f)return false;
        owned=true;
      }
      progress=std::clamp(inward/g.travel,0.f,2.f);
      y=std::clamp(sy_+dy*.35f,float(g.min_y),float(g.max_y));
      ready=inward>=g.travel&&std::abs(dy)*2<=inward;
      return true;
    }
    contact=false;
    if(!owned)return false;
    settle(ready&&static_cast<uint32_t>(now-started_)<=g.maximum_duration_ms,now,animations);
    return true;
  }

void EdgeNavigation::cancel(uint32_t now,bool animations) {
    const bool had_contact=contact;
    blocked_=true;contact=false;finished_=false;
    // Invalid reads can repeat during an I2C outage. Do not restart rebound.
    if(committing_||had_contact||!moving)settle(false,now,animations);
  }

bool EdgeNavigation::tick(uint32_t now,bool animations) {
    if(!moving)return false;
    const float previous=progress;
    const auto duration=committing_?edge_navigation_layout::commit_ms:edge_navigation_layout::rebound_ms;
    const float t=animations?lunaros::graphics::animation_progress(uint32_t(now-motion_),duration):1.f;
    progress=from_*(1-t);
    if(t>=1){moving=owned=false;finished_=committing_;}
    return previous!=progress||t>=1;
  }
}
