#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>

namespace lunaredu::lunaros::graphics {
// Logical RGB565 canvas. Stride is measured in pixels, independently of panel I/O.
struct CanvasSpec {
  uint16_t width = 240, height = 240, stride = 240;
  constexpr bool valid() const {
    return width && height && stride >= width &&
        size_t(stride) <= std::numeric_limits<uint32_t>::max() / height / sizeof(uint16_t);
  }
  constexpr size_t pixels() const { return size_t(stride) * height; }
  constexpr size_t bytes() const { return pixels() * sizeof(uint16_t); }
  constexpr bool operator==(CanvasSpec other) const {
    return width == other.width && height == other.height && stride == other.stride;
  }
};
struct CanvasPoint { int x = 0, y = 0; bool valid = false; };
// Explicit centered viewport; rejects letterbox contacts instead of clamping
// them into interactive controls. Panel rotation remains a driver concern.
struct CanvasViewport {
  CanvasSpec logical;
  int x = 0, y = 0, width = 0, height = 0;
  constexpr bool valid() const {
    return logical.valid() && x >= 0 && y >= 0 && width > 0 && height > 0 &&
        int64_t(x) + width <= std::numeric_limits<int>::max() &&
        int64_t(y) + height <= std::numeric_limits<int>::max();
  }
  constexpr CanvasPoint to_logical(int px, int py) const {
    if (!valid() || px < x || py < y || int64_t(px) >= int64_t(x) + width || int64_t(py) >= int64_t(y) + height) return {};
    return {int((int64_t(px) - x) * logical.width / width),
        int((int64_t(py) - y) * logical.height / height), true};
  }
  constexpr CanvasPoint to_panel(int lx, int ly) const {
    if (!valid() || lx < 0 || ly < 0 || lx >= logical.width || ly >= logical.height) return {};
    return {int(int64_t(x) + int64_t(lx) * width / logical.width),
        int(int64_t(y) + int64_t(ly) * height / logical.height), true};
  }
};
}
