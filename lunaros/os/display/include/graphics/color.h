#pragma once
#include <cstdint>

namespace lunaredu::lunaros::raster {
// Existing painters use both floor and nearest rounding. Make that choice
// explicit so sharing the arithmetic preserves their exact RGB565 output.
enum class BlendRounding : uint8_t { Floor, Nearest };
constexpr uint16_t blend_rgb565(uint16_t background, uint16_t foreground,
                              uint8_t alpha,
                              BlendRounding rounding = BlendRounding::Nearest) {
  if(!alpha)return background;
  if(alpha==255)return foreground;
  const unsigned inverse = 255u - alpha;
  const unsigned bias = rounding == BlendRounding::Nearest ? 127u : 0u;
  const auto channel = [&](unsigned shift, unsigned mask) {
    return ((((foreground >> shift) & mask) * alpha +
             ((background >> shift) & mask) * inverse + bias) / 255u) << shift;
  };
  return static_cast<uint16_t>(channel(11,31) | channel(5,63) | channel(0,31));
}
} // namespace lunaredu::lunaros::raster
