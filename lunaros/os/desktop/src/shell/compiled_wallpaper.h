#pragma once
#include "desktop/graphics/canvas_spec.h"
#include "graphics/color.h"
#include <array>
namespace lunaredu::desktop::compiled_wallpaper {
struct Row {uint16_t base;int16_t center;};
constexpr auto compile_rows(){
  std::array<std::array<Row,canvas_layout::side>,3> result{};
  constexpr uint16_t top[]{0x01c9,0x0185,0x30c9},bottom[]{0x0804,0x1048,0x0885};
  for(unsigned style=0;style<3;++style)for(int y=0;y<canvas_layout::side;++y){
    const int center=style==0?lunaros::graphics::metrics::px(160)-y/3:
      style==1?lunaros::graphics::metrics::px(40)+y/2:lunaros::graphics::metrics::px(200)-y/2;
    result[style][y]={lunaros::raster::blend_rgb565(top[style],bottom[style],uint8_t(y*255/canvas_layout::last)),int16_t(center)};
  }
  return result;
}
constexpr auto compile_opacity(){
  std::array<uint8_t,canvas_layout::side> result{};
  constexpr int span=lunaros::graphics::metrics::px(90);
  for(int distance=0;distance<canvas_layout::side;++distance)result[distance]=uint8_t(std::max(0,span-distance)*60/span);
  return result;
}
inline constexpr auto rows=compile_rows();
inline constexpr auto opacity=compile_opacity();
inline constexpr uint16_t glow[]{0x02ad,0x03c9,0x69cb};
}
