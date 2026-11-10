#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

namespace smart_speaker {

struct FrameRegion { int x = 0, y = 0, width = 0, height = 0; };
struct FrameRegions {
  std::array<FrameRegion, 20> items{};
  unsigned count = 0;
};

// Compare canonical tiles, including pixels that became black. Bridge small
// gaps to amortize the LCD address-window commands. No allocation or hashing.
inline FrameRegions frame_delta(const uint16_t *previous, const uint16_t *next,
                                int width, int rows, bool force) {
  FrameRegions result;
  if (force || !previous) {
    result.items[result.count++] = {0, 0, width, rows};
    return result;
  }
  std::array<int, 160> first, last;
  first.fill(rows); last.fill(-1);
  for (int y = 0; y < rows; ++y)
    for (int x = 0; x < width; ++x)
      if (previous[y * width + x] != next[y * width + x]) {
        first[x] = std::min(first[x], y); last[x] = y;
      }
  for (int x = 0; x < width;) {
    while (x < width && last[x] < 0) ++x;
    if (x == width) break;
    const int start = x;
    int end = x, top = rows, bottom = -1;
    for (; x < width; ++x) {
      if (last[x] >= 0) {
        end = x; top = std::min(top, first[x]); bottom = std::max(bottom, last[x]);
      } else if (x - end > 16) break;
    }
    result.items[result.count++] = {start, top, end - start + 1, bottom - top + 1};
  }
  return result;
}
} // namespace smart_speaker
