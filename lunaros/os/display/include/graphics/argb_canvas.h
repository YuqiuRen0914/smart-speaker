#pragma once
#include "lunaros_text_raster.h"
#include "graphics/icon_draw.h"
#include <algorithm>
#include <cstdint>

namespace lunaredu::lunaros::graphics::canvas {
// Borrowed transparent overlay storage. Raster masks replace coverage in the
// destination; ScreenReservedOverlayComposer blends it into the RGB565 frame.
struct ArgbSurface {
  uint32_t *pixels = nullptr;
  int width = 0, height = 0, stride = 0;
  void pixel(int x, int y, uint32_t ink) const {
    if (pixels && x >= 0 && y >= 0 && x < width && y < height)
      pixels[y * stride + x] = ink;
  }
  void rectangle(int x0, int y0, int x1, int y1, uint32_t ink) const {
    for (int y = std::max(0, y0); y <= std::min(height - 1, y1); ++y)
      for (int x = std::max(0, x0); x <= std::min(width - 1, x1); ++x)
        pixel(x, y, ink);
  }
  void icon(icons::Icon id, int x, int y, uint32_t ink) const {
    struct Context { const ArgbSurface *surface; uint32_t ink; } context{this, ink};
    graphics::draw_icon(id, x, y, [](void *opaque, int px, int py, uint8_t alpha) {
      const auto &c = *static_cast<Context *>(opaque);
      c.surface->pixel(px, py, (uint32_t(alpha) << 24) | (c.ink & 0xffffffU));
    }, &context);
  }
  void text(const char *value, unsigned size, int x, int y, int right,
            uint32_t ink) const {
    if (!value || !text_raster::ready() || x >= right) return;
    struct Context { const ArgbSurface *surface; int right; uint32_t ink; } context{this, right, ink};
    text_raster::draw(x, y, value, size, [](void *opaque, int px, int py, uint8_t alpha) {
      const auto &c = *static_cast<Context *>(opaque);
      if (px < c.right)
        c.surface->pixel(px, py, (uint32_t(alpha) << 24) | (c.ink & 0xffffffU));
    }, &context);
  }
};
}
