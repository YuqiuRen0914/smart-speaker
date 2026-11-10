#pragma once
#include "graphics/scene.h"

namespace lunaredu::lunaros::graphics {
// Synchronous LVGL drawing on an exclusively borrowed RGB565 canvas. Only the
// existing render owner calls this API. Each operation finishes before its
// text, image descriptor or source pixels can leave scope; no output is kept.
// Scene::canvas supplies a retained region with page-coordinate bounds; raw
// paint_canvas callbacks use the ordinary zero-origin output coordinates.
class CanvasPainter {
 public:
  CanvasPainter(const CanvasPainter &)=delete;
  CanvasPainter &operator=(const CanvasPainter &)=delete;
  void clip(Rect);
  bool rectangle(Rect,uint16_t color,int radius=0,int border=0,uint8_t opacity=255);
  bool line(float x1,float y1,float x2,float y2,uint16_t color,uint8_t opacity=255,unsigned width=1,bool rounded=false);
  bool text(int x,int y,const char *,unsigned size,uint16_t color);
  // Single-line official Label fitting, vertically centered in the given box.
  bool label(Rect,const char *,unsigned size,uint16_t color,Align=Align::Left,bool ellipsis=true);
  Rect text_bounds(const char *,unsigned size);
  // Nonzero revision opts immutable source coverage into the bounded owner
  // cache. A mutable borrowed source must keep the default zero revision.
  bool mask(Rect,assets::MaskView,assets::MaskBounds,uint16_t color,uint8_t opacity=255,uint64_t revision=0);
  bool image(Rect,const uint16_t *pixels,CanvasSpec source,int radius=0,uint8_t opacity=255);
  bool good() const;
 private:
  friend class Scene;
  struct Workspace {void *native=nullptr;size_t maximum_bytes=64*1024;~Workspace();void reset();};
  struct Impl;
  explicit CanvasPainter(Impl &impl):impl_(&impl){}
  static bool paint_on(Workspace &,void *display,void *fonts,CanvasSpec,RasterOutput,bool antialias,
                       void *context,bool (*paint)(void *,CanvasPainter &),int origin_x=0,int origin_y=0);
  Impl *impl_;
};
}
