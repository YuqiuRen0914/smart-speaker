#pragma once
#include "desktop/graphics/canvas_spec.h"
#include "lunaros_ui_status_layout.generated.h"
#include <cmath>
#include <cstdint>

namespace lunaredu::desktop {
// Input-owner value state, copied with render snapshots. Only the margin owns
// taps; the wider reveal band claims input once downward intent is established.
struct StatusNavigation {
  static constexpr uint32_t idle_ms = status_layout::idleMs;
  bool visible = false, contact = false, blocked = false, held = false;
  bool open_system = false, reveal_contact = false, dragged = false;
  bool pinned = false, low_battery = false;
  bool captured = false, reveal_rejected = false;
  uint32_t last_activity = 0, started = 0;
  float start_x = 0, start_y = 0;

  bool showing(bool automatic) const { return !automatic || visible || pinned; }
  void cancel() { contact=false; captured=false; held=false; blocked=true; open_system=false; }
  void activity(uint32_t now) { last_activity=now; }
  bool update(uint32_t now,bool automatic,bool critical,bool low);
  bool pointer(bool pressed,bool valid,float x,float y,uint32_t now,bool automatic) {return pointer_impl(pressed,valid,x,y,now,automatic,geometry());}

 private:
  struct Geometry {int side; int height; int inset; int slop;};
  static constexpr Geometry geometry() {return {canvas_layout::side,status_layout::height,lunaros::graphics::metrics::inset,lunaros::graphics::metrics::gesture_slop};}
  bool pointer_impl(bool pressed,bool valid,float x,float y,uint32_t now,bool automatic,const Geometry &g);
 public:
  bool take_system() { open_system=false;return false; }
};
} // namespace lunaredu::desktop
