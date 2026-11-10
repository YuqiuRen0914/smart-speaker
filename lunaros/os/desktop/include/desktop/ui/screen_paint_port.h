#pragma once
#include "desktop/graphics/canvas.h"
#include "graphics/scene_runtime.h"
namespace lunaredu::desktop {
class ScreenPaintPort {
 public:
  virtual ~ScreenPaintPort() = default;
  virtual bool begin_canvas() = 0;
  virtual bool begin_widgets() = 0;
  virtual lunaros::graphics::SceneRuntime &graphics() = 0;
  virtual lunaros::graphics::canvas::Surface surface() = 0;
  // Null title explicitly omits system chrome (e.g. startup).
  virtual bool finish_widgets(const char *title, bool orient = true) = 0;
  virtual void rotate(unsigned) = 0;
  virtual void present_widgets() = 0;
  virtual void flush() = 0;
};
}
