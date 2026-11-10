#pragma once
#include "display/pipeline/geometry.h"
#include <algorithm>
namespace lunaredu::display::screen_detail {
inline bool valid_region(ScreenRegion r, int width, int height) {
  return r.x >= 0 && r.y >= 0 && r.width >= 0 && r.height >= 0 &&
         r.x <= width && r.y <= height && r.width <= width - r.x &&
         r.height <= height - r.y;
}

inline bool worth_merging(ScreenRegion a, ScreenRegion b) {
  const int left = std::min(a.x, b.x);
  const int top = std::min(a.y, b.y);
  const int right = std::max(a.x + a.width, b.x + b.width);
  const int bottom = std::max(a.y + a.height, b.y + b.height);
  const uint64_t combined = static_cast<uint64_t>(right - left) *
                            static_cast<uint64_t>(bottom - top);
  const uint64_t separate = static_cast<uint64_t>(a.width) * a.height +
                            static_cast<uint64_t>(b.width) * b.height;
  return a.x <= b.x + b.width && b.x <= a.x + a.width &&
         a.y <= b.y + b.height && b.y <= a.y + a.height &&
         combined <= separate;
}

inline ScreenRegion united(ScreenRegion a, ScreenRegion b) {
  const int right = std::max(a.x + a.width, b.x + b.width);
  const int bottom = std::max(a.y + a.height, b.y + b.height);
  a.x = std::min(a.x, b.x);
  a.y = std::min(a.y, b.y);
  a.width = right - a.x;
  a.height = bottom - a.y;
  return a;
}
}
