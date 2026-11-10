#pragma once
#include "lunaros_asset_icons_mask.h"

namespace lunaredu::lunaros::graphics {
enum class Sampling : uint8_t { Source, Threshold, Antialias };
using MaskPixel = void (*)(void *context, int x, int y, uint8_t alpha);
// Scaling, coverage sampling and clipping apply to any immutable mask.
// The caller owns color blending and its framebuffer. No shared scratch.
bool draw_mask(assets::MaskView view, int x, int y, uint16_t width, uint16_t height,
               MaskPixel pixel, void *context, Sampling sampling = Sampling::Source);
bool draw_mask(assets::MaskView view, int x, int y, MaskPixel pixel, void *context,
               Sampling sampling = Sampling::Source);
bool draw_mask_fitted(assets::MaskView view, assets::MaskBounds bounds,
                      int x, int y, uint16_t width, uint16_t height,
                      MaskPixel pixel, void *context, Sampling sampling = Sampling::Source);
} // namespace lunaredu::lunaros::graphics
