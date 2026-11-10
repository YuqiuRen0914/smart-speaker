#pragma once
#include <cstdint>
namespace lunaredu::lunaros::raster {
enum class ImageSampling : uint8_t { Nearest, Linear };
enum class RasterContent : uint8_t { Image, PixelArt, QrCode };

struct RenderPolicy {
  bool antialias = true;
  ImageSampling images = ImageSampling::Nearest;
  constexpr ImageSampling sampling(RasterContent content) const {
    return content == RasterContent::Image ? images : ImageSampling::Nearest;
  }
};
}
