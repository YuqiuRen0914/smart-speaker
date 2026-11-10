#pragma once
#include "desktop/ui/screen_paint_port.h"
#include "desktop/shell/shell.h"
#include "display/pipeline/frame_damage.h"
#include "display/frame.h"
namespace lunaredu::desktop {
// Borrowed by one render call. Applications own content; OS owns the canvas,
// status chrome, orientation, submission and immutable Shell snapshot.
class UiPaintPort : public ScreenPaintPort {
 public:
  struct RetainedSurface {lunaros::graphics::canvas::Surface canvas;bool retained=false;display::Damage overwritten;};
  virtual RetainedSurface retained_surface(const void *,uint64_t) {return {surface(),false,{}};}
  virtual bool begin_region(uint16_t width, uint16_t height, uint16_t color,
      uint16_t origin_y = 0, bool clear = true) = 0;
  virtual bool flush_region(uint16_t x, uint16_t y) = 0;
  // Borrowed until flush_region consumes the screen lease. Not a second canvas.
  virtual display::FrameCanvas submission_canvas() const { return {}; }
  // Complete final-pixel candidates against the previous App presentation.
  // System composition extends this map; recovery/identity changes ignore it.
  virtual void damage_map(const display::FrameDamageMap &) {}
  // All final App differences must be inside this coverage, relative to the
  // last submitted frame. OS extends system coverage; recovery forces full I/O.
  // Conservative extra pixels trade bus bandwidth for avoiding a pixel scan.
  void complete_damage_map(display::FrameDamageMap map){map.use_declared_region();damage_map(map);}
  virtual void header(const char *title) = 0;
  virtual void overlay(const char *title) = 0;
  virtual unsigned rotation() const = 0;
  virtual const DesktopShell &shell() const = 0;
};
}
