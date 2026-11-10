#include "desktop/navigation/home_navigation.h"
#include "graphics/animation.h"
namespace lunaredu::desktop {
void HomeNavigation::cancel_impl(uint32_t now,bool animations,const Geometry &g) {
    blocked_=true;const bool was_contact=contact;contact=false;
    if(!committing_)requested_=false;
    // Repeated invalid samples must not restart a rebound.
    if(!committing_&&progress>0&&(was_contact||!moving))settle(false,now,animations,g);
  }

bool HomeNavigation::pointer_impl(bool pressed,bool valid,float x,float y,uint32_t now,bool animations,const Geometry &g) {
    if(!enabled)return false;
    if(committing_)return true;
    if(!valid||(pressed&&(!std::isfinite(x)||!std::isfinite(y)||x<0||x>=g.side||y<0||y>=g.side))){
      const bool owned=contact;cancel_impl(now,animations,g);return owned;
    }
    if(blocked_){if(!pressed)blocked_=false;return pressed;}
    if(pressed&&!contact){
      if(y<g.side-g.edgeHeight)return false;
      if(moving)tick_impl(now,animations,g);
      moving=false;contact=true;dragging_=progress>0;
      base_=progress;sx_=lx_=x;sy_=ly_=y;started_=sampled_=now;max_travel_=velocity_=0;
      // A rebound can be grabbed again without recapturing a transformed frame.
      if(progress==0)++epoch;
      return true;
    }
    if(!contact)return false;
    if(pressed){lx_=x;ly_=y;max_travel_=std::max(max_travel_,std::hypot(x-sx_,y-sy_));}
    const float up=sy_-ly_,side=std::abs(lx_-sx_);
    const bool vertical=up>=0&&side*g.directionRatio<=up;
    if(pressed){
      if(dragging_||(vertical&&up>=g.dragDeadzone)){
        dragging_=true;show(now,animations);
        const float next=animations?std::clamp(base_+(up-(base_>0?0:g.dragDeadzone))/g.dragSpan,0.f,.9f):0;
        const uint32_t dt=now-sampled_;
        if(dt>0)velocity_=dt<=120?std::clamp((next-progress)*1000/dt,-8.f,8.f):0;
        progress=next;sampled_=now;
      }
      return true;
    }
    contact=false;idle_=now;
    const uint32_t elapsed=now-started_;
    const bool tap=elapsed<=500&&max_travel_<=12&&std::abs(sx_-g.center)<=g.tapWidth/2;
    if(now-sampled_>120)velocity_=0;
    if(vertical&&up>=g.travel&&elapsed<=g.maximumDurationMs){requested_=true;committing_=true;settle(true,now,animations,g);}
    else {if(tap)show(now,animations);if(progress>0)settle(false,now,animations,g);}
    return true;
  }

bool HomeNavigation::tick_impl(uint32_t now,bool animations,const Geometry &g) {
    bool changed=false;
    if(visible&&!contact&&!moving&&!committing_&&now-idle_>=g.idleDurationMs){visible=false;fade_from_=opacity;fade_=now;changed=true;}
    if(!moving && opacity==(visible?1.f:0.f))return changed;
    const float t=animations?lunaros::graphics::animation_progress(uint32_t(now-fade_),g.revealDurationMs,lunaros::graphics::AnimationCurve::Linear):1.f;
    const float alpha=fade_from_+((visible?1.f:0.f)-fade_from_)*t;
    changed|=std::abs(alpha-opacity)>.001f;opacity=alpha;
    if(moving){
      const float duration=committing_?g.transitionDurationMs:g.reboundDurationMs;
      const float u=animations?std::min(1.f,float(now-motion_)/duration):1.f,u2=u*u,u3=u2*u;
      if(committing_)completion=(slope_-2)*u3+(3-2*slope_)*u2+slope_*u;
      else progress=std::clamp((2*u3-3*u2+1)*from_+(u3-2*u2+u)*rebound_velocity_*duration/1000,0.f,.9f);
      changed=true;
      if(u>=1){moving=false;idle_=now;if(committing_)finished_=true;else progress=0;}
    }
    return changed;
  }

void HomeNavigation::settle(bool returning,uint32_t now,bool animations,const Geometry &g) {
    from_=progress;motion_=now;moving=true;
    const float duration=g.reboundDurationMs/1000.f;
    rebound_velocity_=std::clamp(velocity_,-3*from_/duration,1.2f*from_/duration);
    slope_=std::clamp(1.2f+std::abs(velocity_)*.1f,1.2f,2.2f);
    if(!animations){moving=false;if(returning){completion=1;finished_=true;}else progress=0;}
  }
}
