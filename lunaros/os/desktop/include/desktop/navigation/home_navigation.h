#pragma once
#include "desktop/graphics/canvas_spec.h"
#include "lunaros_ui_interaction_layout.generated.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace lunaredu::desktop {
// Input-owned value state, copied into immutable render snapshots.
class HomeNavigation {
 public:
  bool enabled=false, visible=false, contact=false, moving=false;
  // Drag pose and return completion are independent, so release never jumps.
  float progress=0, completion=0, opacity=0;
  uint32_t epoch=0;
  void reset(bool value=false) {
    const uint32_t next=epoch+1;*this={};epoch=next;enabled=value;blocked_=true;
  }
  void cancel(uint32_t now,bool animations=true) {cancel_impl(now,animations,geometry());}
  bool pointer(bool pressed,bool valid,float x,float y,uint32_t now,bool animations=true) {return pointer_impl(pressed,valid,x,y,now,animations,geometry());}
  bool tick(uint32_t now,bool animations=true) {return tick_impl(now,animations,geometry());}
  bool take_request(){const bool value=requested_;requested_=false;return value;}
  bool take_finished(){const bool value=finished_;finished_=false;return value;}
  bool exiting() const{return committing_;}

 private:
  struct Geometry {int side; float center; int edgeHeight; int directionRatio; int dragDeadzone; int dragSpan; int tapWidth; int travel; uint32_t maximumDurationMs; uint32_t idleDurationMs; int revealDurationMs; int transitionDurationMs; int reboundDurationMs;};
  static constexpr Geometry geometry() {return {canvas_layout::side,canvas_layout::center,home_gesture_layout::edgeHeight,home_gesture_layout::directionRatio,home_gesture_layout::dragDeadzone,home_gesture_layout::dragSpan,home_gesture_layout::tapWidth,home_gesture_layout::travel,home_gesture_layout::maximumDurationMs,home_gesture_layout::idleDurationMs,home_gesture_layout::revealDurationMs,home_gesture_layout::transitionDurationMs,home_gesture_layout::reboundDurationMs};}
  void cancel_impl(uint32_t now,bool animations,const Geometry &g);
  bool pointer_impl(bool pressed,bool valid,float x,float y,uint32_t now,bool animations,const Geometry &g);
  bool tick_impl(uint32_t now,bool animations,const Geometry &g);

  void show(uint32_t now,bool animations){if(!visible){visible=true;fade_from_=opacity;fade_=now;}idle_=now;if(!animations)opacity=1;}
  void settle(bool returning,uint32_t now,bool animations,const Geometry &g);
  bool blocked_=false,dragging_=false,requested_=false,finished_=false,committing_=false;
  float sx_=0,sy_=0,lx_=0,ly_=0,max_travel_=0,base_=0,velocity_=0,fade_from_=0,from_=0,rebound_velocity_=0,slope_=1.2f;
  uint32_t started_=0,sampled_=0,idle_=0,fade_=0,motion_=0;
};
}
