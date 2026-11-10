#pragma once
#include <cstdint>
#include "graphics/color.h"

namespace lunaredu::lunaros::text_raster {
// Single-channel glyph coverage: never LCD/subpixel color fringes.
inline uint8_t text_alpha(uint8_t coverage, bool antialias) {
  return antialias ? coverage : coverage >= 128 ? 255 : 0;
}
inline uint16_t paint_rgb565(uint16_t background, uint16_t foreground,
                             uint8_t coverage, bool antialias,uint8_t opacity=255) {
  // Edge coverage may be quantized; intentional transparency never is.
  const unsigned alpha=(unsigned(text_alpha(coverage,antialias))*opacity+127)/255;
  return raster::blend_rgb565(background,foreground,uint8_t(alpha));
}
} // namespace lunaredu::lunaros::text_raster
