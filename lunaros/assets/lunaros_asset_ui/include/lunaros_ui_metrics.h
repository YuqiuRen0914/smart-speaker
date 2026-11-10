#pragma once
#include <algorithm>
#include <array>
#include <cstdint>

// One immutable profile per firmware. The product declares its active area;
// all conversion and density decisions below are required constant expressions.
#ifndef LUNAROS_UI_SIDE
#define LUNAROS_UI_SIDE 240
#endif
#ifndef LUNAROS_UI_ACTIVE_WIDTH_UM
#define LUNAROS_UI_ACTIVE_WIDTH_UM 0
#endif
#ifndef LUNAROS_UI_TEXT_PERCENT
#define LUNAROS_UI_TEXT_PERCENT 100
#endif
namespace lunaredu::lunaros::graphics::metrics {
inline constexpr int side=LUNAROS_UI_SIDE;
inline constexpr int active_width_um=LUNAROS_UI_ACTIVE_WIDTH_UM;
inline constexpr bool native=side!=240;
static_assert(side>=240 && side<=800,"Unsupported square UI canvas");
static_assert(!native || (active_width_um>=20000 && active_width_um<=200000),
              "Native UI requires the actual active width in micrometres");
static_assert(LUNAROS_UI_TEXT_PERCENT>=80 && LUNAROS_UI_TEXT_PERCENT<=160,
              "UI text scale must be 80..160 percent");
// Reference coordinates support fixed diagrams; adaptive lists/grid use their
// own compiled geometry instead of scaling a complete small-screen image.
constexpr int px(int reference) { return reference*side/240; }
constexpr int um(int physical) {
  return native?int((int64_t(physical)*side+active_width_um/2)/active_width_um):0;
}
constexpr int font(int reference) {
  if(!native)return reference;
  const int physical=reference<=10?2200:reference<=12?2700:reference<=14?3000:
    reference<=16?3300:reference<=20?3800:reference<=24?4200:
    reference<=32?6000:reference<=48?8000:11000;
  return std::max(12,um(physical*LUNAROS_UI_TEXT_PERCENT/100));
}
constexpr int line_height(int size,int reference_line) {
  return native?font(size)+(font(size)+3)/4:reference_line;
}
inline constexpr int ppi_milli=native?int(int64_t(side)*25400000/active_width_um):0;
inline constexpr int inset=native?um(1900):8;
inline constexpr int gap=native?um(1400):8;
inline constexpr int min_touch=native?um(9000):44;
inline constexpr int status_height=native?std::max(um(4200),font(12)+gap):28;
inline constexpr int row_icon=native?um(5500):30;
inline constexpr int launcher_icon=native?um(7500):60;
inline constexpr int row_min=native?std::max(min_touch,font(16)+font(12)+gap*2):0;
inline constexpr int gesture_edge=native?um(2200):8;
inline constexpr int gesture_travel=native?um(6500):48;
inline constexpr int gesture_slop=native?um(1100):6;
inline constexpr int tab_height=native?min_touch:26;
// User text preferences are independent of the firmware's physical baseline.
// Bounded presets are compiled once; the render owner only selects a table.
inline constexpr std::array<unsigned,3> text_percentages{{90,100,115}};
constexpr auto make_text_sizes() {
  std::array<std::array<uint8_t,129>,3> result{};
  for(unsigned preset=0;preset<result.size();++preset)
    for(unsigned size=1;size<result[preset].size();++size)
      result[preset][size]=uint8_t(std::clamp((size*text_percentages[preset]+50)/100,1u,128u));
  return result;
}
inline constexpr auto text_sizes=make_text_sizes();
constexpr unsigned text_scale_percent(unsigned preset) {
  return text_percentages[preset<text_percentages.size()?preset:1];
}
constexpr unsigned scaled_text_size(unsigned size,unsigned preset) {
  // Leave invalid font inputs invalid instead of silently admitting them.
  return size<text_sizes[0].size()?text_sizes[preset<text_sizes.size()?preset:1][size]:size;
}
}
