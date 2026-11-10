#pragma once
#include "lunaros_text_raster.h"
#include "graphics/text_paint.h"
#include <algorithm>
namespace lunaredu::lunaros::graphics::canvas {
// Borrowed pixels. The caller supplies dimensions, colors and typography.
struct Rgb565Canvas {
  uint16_t *pixels;
  int width, height, stride;
  bool antialias;
  void pixel(int x,int y,uint16_t ink,uint8_t alpha=255,uint8_t opacity=255) const {
    if(!pixels||x<0||y<0||x>=width||y>=height)return;
    auto &bg=pixels[y*stride+x];bg=text_raster::paint_rgb565(bg,ink,alpha,antialias,opacity);
  }
  void rect(int x,int y,int w,int h,uint16_t ink) const {
    if(!pixels||w<=0||h<=0)return;
    const int left=std::max(0,x),right=std::min(width,x+w);
    if(right<=left)return;
    for(int row=std::max(0,y);row<std::min(height,y+h);++row)
      std::fill_n(pixels+row*stride+left,right-left,ink);
  }
  void text(int x,int y,const char *value,unsigned size,uint16_t ink,
            int left,int top,int w,int h) const {
    if(!value||!size)return;
    struct Context {const Rgb565Canvas *surface;int x,y,w,h;uint16_t ink;} c{this,left,top,w,h,ink};
    text_raster::draw(x,y,value,size,[](void *p,int px,int py,uint8_t alpha){
      const auto &c=*static_cast<Context *>(p);
      if(px>=c.x&&py>=c.y&&px<c.x+c.w&&py<c.y+c.h)c.surface->pixel(px,py,c.ink,alpha);
    },&c);
  }
  void label(int x,int y,int w,int h,const char *value,unsigned size,uint16_t ink,bool centered) const {
    if(!value||w<=0||h<=0)return;
    const auto b=text_raster::bounds(value,size);
    text(centered&&b.width<=w?x+(w-b.width)/2-b.left:x,y+(h-b.height)/2-b.top,
         value,size,ink,x,y,w,h);
  }
};
}
