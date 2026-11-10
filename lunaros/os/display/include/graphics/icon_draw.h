#pragma once
#include "lunaros_asset_icons.h"
#include "graphics/mask_coverage.h"

namespace lunaredu::lunaros::graphics {
inline bool draw_icon(icons::Icon id, int x, int y, uint16_t width, uint16_t height,
                      MaskPixel pixel, void *context, Sampling sampling = Sampling::Source) {
  return draw_mask(icons::builtin(id), x, y, width, height, pixel, context, sampling);
}
inline bool draw_icon(icons::Icon id, int x, int y, MaskPixel pixel, void *context,
                      Sampling sampling = Sampling::Source) {
  return draw_mask(icons::builtin(id), x, y, pixel, context, sampling);
}
inline bool draw_icon_fitted(icons::Icon id, int x, int y, uint16_t width, uint16_t height,
                             MaskPixel pixel, void *context, Sampling sampling = Sampling::Source) {
  return draw_mask_fitted(icons::builtin(id), icons::bounds(id), x, y, width, height,
                          pixel, context, sampling);
}
} // namespace lunaredu::lunaros::graphics
