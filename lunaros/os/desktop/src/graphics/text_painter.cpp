#include "desktop/ui/text_painter.h"
#include "desktop/graphics/canvas.h"
#include "desktop/graphics/canvas_spec.h"

namespace lunaredu::desktop {
DesktopPainter screen_text_painter() {
  return {nullptr,[](void *,uint16_t *pixels,int x,int y,const char *value,int size,uint16_t color,bool aa){
    lunaros::graphics::canvas::Surface{pixels,kDesktopCanvas.width,kDesktopCanvas.height,kDesktopCanvas.stride,aa}.text(x,y,value,size,color);
  },[](void *,const char *value,int size){return lunaros::text_raster::width(value,size);},
  [](void *,const char *value,int size){const auto b=lunaros::text_raster::bounds(value,size);return DesktopPainter::TextBounds{b.left,b.top,b.width,b.height};},
  [](void *,uint16_t *pixels,int x,int y,const char *value,int size,uint16_t color,bool aa,int cx,int cy,int cw,int ch){
    lunaros::graphics::canvas::Surface{pixels,kDesktopCanvas.width,kDesktopCanvas.height,kDesktopCanvas.stride,aa}.text(x,y,value,size,color,{cx,cy,cw,ch});
  }};
}
}
