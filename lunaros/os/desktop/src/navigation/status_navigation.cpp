#include "desktop/navigation/status_navigation.h"
namespace lunaredu::desktop {
bool StatusNavigation::pointer_impl(bool pressed,bool valid,float x,float y,uint32_t now,bool automatic,const Geometry &g) {
    if(!valid||(pressed&&(!std::isfinite(x)||!std::isfinite(y)||x<0||x>=g.side||y<0||y>=g.side))){
      const bool owned=contact&&captured;cancel();return owned;
    }
    if(blocked){if(!pressed)blocked=false;return true;}
    const bool was_held=held;held=pressed;
    if(pressed){
      if(showing(automatic))activity(now);
      if(!contact){
        if(was_held||y>=g.height)return false;
        contact=true;start_x=x;start_y=y;started=now;dragged=false;
        reveal_contact=!showing(automatic);
        captured=!reveal_contact||y<g.inset;
        reveal_rejected=false;
        return captured;
      }
      const float dx=std::abs(x-start_x),dy=y-start_y;
      const int slop=g.slop;
      dragged|=std::hypot(dx,dy)>=slop;
      if(reveal_contact&&!reveal_rejected&&!visible){
        if(dy>=slop&&dy>=dx*1.25f){captured=true;visible=true;activity(now);}
        else if(dy<=-slop||(dx>=slop&&dx>dy))reveal_rejected=true;
      }
      return captured;
    }
    if(was_held)activity(now);
    if(!contact)return false;
    const bool owned=captured;contact=false;captured=false;
    if(!owned)return false;
    activity(now);
    if(!dragged&&!reveal_rejected&&uint32_t(now-started)<=500){
      if(reveal_contact)visible=true;
      // A visible status bar is informational. Consume taps without navigating.
    }
    return true;
  }

bool StatusNavigation::update(uint32_t now,bool automatic,bool critical,bool low) {
    const bool before=showing(automatic);
    pinned=critical;
    if(low&&!low_battery){visible=true;activity(now);}
    low_battery=low;
    if(automatic&&visible&&!pinned&&!held&&uint32_t(now-last_activity)>=idle_ms)
      visible=false;
    return before!=showing(automatic);
  }
}
