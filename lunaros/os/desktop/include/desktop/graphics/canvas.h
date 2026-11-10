#pragma once
#include "lunaros_ui_theme.h"
#include "desktop/graphics/input_hint.h"
#include "lunaros_text_raster.h"
#include "graphics/rgb565_canvas.h"
#include "graphics/text_paint.h"
#include "graphics/icon_draw.h"
#include <algorithm>
#include <cmath>

namespace lunaredu::lunaros::graphics::canvas {
namespace type=desktop::typography;
// Borrowed framebuffer. No allocation, buffer ownership or input authority.
struct Surface {
  uint16_t *pixels=nullptr;
  int width=metrics::side,height=metrics::side,stride=metrics::side;
  bool antialias=true;
  lunaros::graphics::canvas::Rgb565Canvas raster() const { return {pixels,width,height,stride,antialias}; }
  void pixel(int x,int y,uint16_t ink,uint8_t alpha=255,uint8_t opacity=255) const { raster().pixel(x,y,ink,alpha,opacity); }
  void rect(Rect box,uint16_t ink) const { raster().rect(box.x,box.y,box.width,box.height,ink); }
  void text(int x,int y,const char *value,unsigned size,uint16_t ink,Rect clip={0,0,metrics::side,metrics::side}) const {
    raster().text(x,y,value,size,ink,clip.x,clip.y,clip.width,clip.height);
  }
  void label(Rect box,const char *value,unsigned size=type::label.size,uint16_t ink=theme::text,bool centered=true) const {
    raster().label(box.x,box.y,box.width,box.height,value,size,ink,centered);
  }
  void icon(icons::Icon id,int x,int y,uint16_t ink,int size=0) const {
    struct Context {const Surface *surface;uint16_t ink;} context{this,ink};
    const auto draw=[](void *opaque,int px,int py,uint8_t alpha){
      const auto &c=*static_cast<Context *>(opaque);c.surface->pixel(px,py,c.ink,alpha);
    };
    const auto sampling=antialias?lunaredu::lunaros::graphics::Sampling::Antialias:lunaredu::lunaros::graphics::Sampling::Threshold;
    if(size>0)lunaredu::lunaros::graphics::draw_icon_fitted(id,x,y,size,size,draw,&context,sampling);
    else lunaredu::lunaros::graphics::draw_icon(id,x,y,draw,&context,sampling);
  }
  void key(Rect box,PhysicalKey key) const {
    const int radius=design::keyDiameter/2,cx=box.x+box.width/2,cy=box.y+box.height/2;
    for(int dy=-radius;dy<=radius;++dy)for(int dx=-radius;dx<=radius;++dx){
      const int distance=dx*dx+dy*dy;
      const auto coverage=uint8_t(std::clamp(radius+0.5f-std::sqrt(float(distance)),0.f,1.f)*255);
      if(!coverage)continue;
      pixel(cx+dx,cy+dy,distance>(radius-1)*(radius-1)?theme::raised:
        key==PhysicalKey::A?(dy< -3?design::keyahighlight:design::keya):(dy< -3?design::keybhighlight:design::keyb),coverage);
    }
    label(box,key==PhysicalKey::A?"A":"B");
  }
  template<size_t count>bool hint(int y,const std::array<HintPart,count> &parts,uint16_t ink=theme::secondary) const {
    auto copy=*this;
    const desktop::DesktopPainter painter{&copy,
      [](void *p,uint16_t *,int x,int y,const char *s,int size,uint16_t c,bool){static_cast<Surface *>(p)->text(x,y,s,size,c);},
      [](void *,const char *s,int size){return text_raster::width(s,size);},
      [](void *,const char *s,int size){const auto b=text_raster::bounds(s,size);return desktop::DesktopPainter::TextBounds{b.left,b.top,b.width,b.height};}};
    return paint_input_hint(painter,pixels,{design::inset,y,design::width,design::hintHeight},parts,
      [](void *p,Rect box,PhysicalKey key){static_cast<Surface *>(p)->key(box,key);},&copy,ink,antialias);
  }
  // Trusted packed 1-bit content supplied by the application; color is content.
  void mask(const uint8_t *bits,int source_width,int source_height,Rect box,uint16_t ink) const {
    if(!bits||source_width<=0||source_height<=0||source_width>64||source_height>64||box.width<=0||box.height<=0||box.width>65535||box.height>65535)return;
    const int source_stride=(source_width+7)/8;
    struct Context {const Surface *surface;uint16_t ink;} context{this,ink};
    draw_mask({bits,static_cast<uint16_t>(source_width),static_cast<uint16_t>(source_height),
               static_cast<uint16_t>(source_stride),assets::MaskFormat::Mono1},
              box.x,box.y,box.width,box.height,[](void *opaque,int x,int y,uint8_t alpha){
                const auto &c=*static_cast<Context *>(opaque);c.surface->pixel(x,y,c.ink,alpha);
              },&context,Sampling::Source);
  }
};
} // namespace lunaredu::lunaros::graphics::canvas
