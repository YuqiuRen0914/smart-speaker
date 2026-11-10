#pragma once
#include <algorithm>
#include <cstring>
#include <cstddef>
#include <cstdint>
#include <lvgl/draw/lv_draw_buf.h>
#include "display/pipeline/frame_damage.h"
namespace lunaredu::lunaros::graphics::detail {
struct RasterCopy { size_t copied=0,compared=0;bool valid=true; };
// Scene initializes LVGL on this owner. Stack views borrow storage; LVGL
// performs the stride-aware copy without allocating or retaining pixels.
inline bool copy_raster_area(const uint16_t *source,uint16_t *output,int width,int height,int stride,
                             const lv_area_t *area=nullptr) {
  lv_draw_buf_t from{},to{};
  const auto bytes=uint32_t(size_t(stride)*height*sizeof(uint16_t));
  if(lv_draw_buf_init(&from,width,height,LV_COLOR_FORMAT_RGB565,stride*2,const_cast<uint16_t *>(source),bytes)!=LV_RESULT_OK ||
     lv_draw_buf_init(&to,width,height,LV_COLOR_FORMAT_RGB565,stride*2,output,bytes)!=LV_RESULT_OK)return false;
  lv_draw_buf_copy(&to,area,&from,area);
  return true;
}
inline RasterCopy copy_raster_output(const uint16_t *source,uint16_t *output,int height,int stride,bool initialized) {
  const auto bytes=size_t(stride)*height*sizeof(uint16_t);
  if(!bytes)return {};
  // Unqualified storage may have been edited by another compositor. Keep one
  // equality check, rather than a second 32-pixel differential renderer.
  if(initialized&&!std::memcmp(source,output,bytes))return {0,bytes};
  if(!copy_raster_area(source,output,stride,height,stride))return {0,initialized?bytes:0,false};
  return {bytes,initialized?bytes:0};
}
inline RasterCopy copy_raster_output(const uint16_t *source,uint16_t *output,int width,int height,int stride,
                                    const display::FrameDamageMap &candidates){
  RasterCopy result;
  for(int y=0;y<height;y+=16){const auto mask=candidates.row(unsigned(y)/16);
    for(unsigned tile=0;tile<unsigned((width+15)/16);){
      if(!(mask&(uint32_t(1)<<tile))){++tile;continue;}
      const int x=int(tile)*16;
      while(tile<unsigned((width+15)/16)&&(mask&(uint32_t(1)<<tile)))++tile;
      const int right=std::min(width,int(tile)*16),bottom=std::min(height,y+16);
      const lv_area_t area{x,y,right-1,bottom-1};
      if(!copy_raster_area(source,output,width,height,stride,&area)){result.valid=false;return result;}
      result.copied+=size_t(right-x)*(bottom-y)*sizeof(uint16_t);
    }
  }
  return result;
}
}
