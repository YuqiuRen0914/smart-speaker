#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>
#include "image_jpeg_encoder.h"

inline bool valid_image_input(const uint8_t *src, size_t size, uint16_t width,
                              uint16_t height, ImagePixelFormat format) {
  if (src == nullptr || size == 0 || size > INT32_MAX) { return false; }
  if (format == ImagePixelFormat::Jpeg) {
    return size >= 4 && src[0] == 0xff && src[1] == 0xd8 &&
           src[size - 2] == 0xff && src[size - 1] == 0xd9;
  }
  // Color conversion uses signed 16-bit dimensions and paired chroma samples.
  if (width == 0 || height == 0 || width > INT16_MAX || height > INT16_MAX) {
    return false;
  }
  const size_t pixels = static_cast<size_t>(width) * height;
  size_t bytes = 0;
  switch (format) {
  case ImagePixelFormat::Gray8: bytes = pixels; break;
  case ImagePixelFormat::Rgb24: bytes = pixels * 3; break;
  case ImagePixelFormat::Rgb565:
  case ImagePixelFormat::Rgb565ByteSwapped:
  case ImagePixelFormat::Yuyv:
  case ImagePixelFormat::Uyvy:
  case ImagePixelFormat::Yuv422Planar: bytes = pixels * 2; break;
  case ImagePixelFormat::Yuv420Planar: bytes = pixels + pixels / 2; break;
  default: return false;
  }
  if (format != ImagePixelFormat::Gray8 && ((width & 1) || (height & 1))) {
    return false;
  }
  // Backend APIs and output capacities are signed int-sized.
  return pixels <= (static_cast<size_t>(INT32_MAX) - 65536) / 3 && size >= bytes;
}
