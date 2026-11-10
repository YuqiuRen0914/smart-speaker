#pragma once

#include <algorithm>
#include <cstdint>

namespace lunaredu::pet::render {

inline uint16_t wire_rgb565(uint32_t rgb, float opacity = 1.0F) {
  opacity = std::clamp(opacity, 0.0F, 1.0F);
  const auto scale = [opacity](uint32_t value) {
    return static_cast<uint32_t>(value * opacity + 0.5F);
  };
  const uint32_t red = scale((rgb >> 16) & 0xffU);
  const uint32_t green = scale((rgb >> 8) & 0xffU);
  const uint32_t blue = scale(rgb & 0xffU);
  const uint16_t canonical = static_cast<uint16_t>(
      ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3));
  return static_cast<uint16_t>((canonical >> 8) | (canonical << 8));
}

} // namespace lunaredu::pet::render
