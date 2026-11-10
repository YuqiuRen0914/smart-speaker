#pragma once
#include "desktop/graphics/canvas_spec.h"
#include "lunaros_ui_interaction_layout.generated.h"
#include "lunaros_ui_status_layout.generated.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace lunaredu::desktop {
// Same interaction dimensions as the PC EdgeNavigation design. All coordinates
// are logical; the product input owner checks sample freshness and rotation.
namespace edge_navigation_layout {
inline constexpr float screen_size=canvas_layout::side, zone=lunaros::graphics::metrics::native?lunaros::graphics::metrics::gesture_edge:28, deadzone=lunaros::graphics::metrics::native?lunaros::graphics::metrics::gesture_slop:7, travel=lunaros::graphics::metrics::native?lunaros::graphics::metrics::gesture_travel:40;
inline constexpr uint32_t maximum_duration_ms=1200, rebound_ms=180, commit_ms=140;
}
class EdgeNavigation {
 public:
  bool enabled=false, contact=false, owned=false, moving=false, ready=false;
  bool right=false;
  float progress=0, y=canvas_layout::center;
  int top_inset=status_layout::height;
  void reset(bool value=false) { *this={};enabled=value;blocked_=true; }
  void cancel(uint32_t now,bool animations=true);
  bool pointer(bool pressed,bool valid,float x,float py,uint32_t now,bool animations=true) {return pointer_impl(pressed,valid,x,py,now,animations,geometry());}
  bool tick(uint32_t now,bool animations=true);
  bool take_finished(){const bool value=finished_;finished_=false;return value;}
  bool exiting() const{return committing_;}

 private:
  struct Geometry {float screen_size; float zone; float deadzone; float travel; uint32_t maximum_duration_ms; int edgeHeight; int min_y; int max_y;};
  static constexpr Geometry geometry() {return {edge_navigation_layout::screen_size,edge_navigation_layout::zone,edge_navigation_layout::deadzone,edge_navigation_layout::travel,edge_navigation_layout::maximum_duration_ms,home_gesture_layout::edgeHeight,lunaros::graphics::metrics::px(60),lunaros::graphics::metrics::px(188)};}
  bool pointer_impl(bool pressed,bool valid,float x,float py,uint32_t now,bool animations,const Geometry &g);

  void settle(bool commit,uint32_t now,bool animations){
    committing_=ready=commit;from_=progress;motion_=now;
    moving=animations&&progress>0;
    if(!moving){progress=0;owned=false;finished_=commit;}
  }
  bool blocked_=false,rejected_=false,committing_=false,finished_=false;
  float sx_=0,sy_=0,lx_=0,ly_=0,from_=0;
  uint32_t started_=0,motion_=0;
};
} // namespace lunaredu::desktop
