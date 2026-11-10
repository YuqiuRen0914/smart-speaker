#include "display/pixel_accelerator.h"
#include <cstring>
#include <limits>
#ifdef ESP_PLATFORM
#include "soc/soc_caps.h"
#if SOC_DMA2D_SUPPORTED && defined(LUNAROS_DISPLAY_DMA2D_AVAILABLE)
#include "esp_async_color_convert.h"
#include "esp_private/dma2d.h"
#endif
#if SOC_PPA_SUPPORTED && defined(LUNAROS_DISPLAY_PPA_AVAILABLE)
#include "driver/ppa.h"
#endif
#endif

namespace lunaredu::display {
namespace {
bool fits(int x, int y, int w, int h, int stride, int height) {
  return x >= 0 && y >= 0 && w > 0 && h > 0 && stride > 0 && height > 0 &&
      x < stride && y < height && w <= stride - x && h <= height - y &&
      size_t(stride) <= std::numeric_limits<size_t>::max() / size_t(height) / 4;
}
}
void PixelAccelerator::initialize() {
  if (initialized_) return;
  initialized_ = true;
#if defined(ESP_PLATFORM) && SOC_DMA2D_SUPPORTED && defined(LUNAROS_DISPLAY_DMA2D_AVAILABLE)
  async_color_convert_config_t c{};
  c.backlog = 1; c.dma_burst_size = 64;
  async_color_convert_handle_t handle = nullptr;
  if (esp_async_color_convert_install_dma2d(&c, &handle) == ESP_OK) {
    dma_ = handle; stats_.dma2d_ready = true;
  }
#endif
#if defined(ESP_PLATFORM) && SOC_PPA_SUPPORTED && defined(LUNAROS_DISPLAY_PPA_AVAILABLE)
  ppa_client_config_t cppa{};
  cppa.oper_type = PPA_OPERATION_BLEND;
  cppa.max_pending_trans_num = 1;
  cppa.data_burst_length = PPA_DATA_BURST_LENGTH_128;
  ppa_client_handle_t client = nullptr;
  if (ppa_register_client(&cppa, &client) == ESP_OK) {
    blend_ = client; stats_.ppa_blend_ready = true;
  }
#endif
}
PixelAccelerator::~PixelAccelerator() {
  // All operations have completed before returning; the owner must be stopped
  // before destruction. No timeout releases a buffer still referenced by DMA.
#if defined(ESP_PLATFORM) && SOC_DMA2D_SUPPORTED && defined(LUNAROS_DISPLAY_DMA2D_AVAILABLE)
  if (dma_) esp_async_color_convert_uninstall(static_cast<async_color_convert_handle_t>(dma_));
#endif
#if defined(ESP_PLATFORM) && SOC_PPA_SUPPORTED && defined(LUNAROS_DISPLAY_PPA_AVAILABLE)
  if (blend_) ppa_unregister_client(static_cast<ppa_client_handle_t>(blend_));
#endif
}
bool PixelAccelerator::copy(PixelBuffer565 dst, int x, int y, const uint16_t *src,
                           int stride, int height, ScreenRegion r) {
  if (!dst.pixels || !src || !fits(x, y, r.width, r.height, dst.stride, dst.height) ||
      !fits(r.x, r.y, r.width, r.height, stride, height)) return false;
  const auto d=reinterpret_cast<uintptr_t>(dst.pixels), s=reinterpret_cast<uintptr_t>(src);
  if ((d<=s && s-d<size_t(dst.stride)*dst.height*2) ||
      (s<d && d-s<size_t(stride)*height*2)) return false;
  const uint64_t bytes = uint64_t(r.width) * r.height * 2;
#if defined(ESP_PLATFORM) && SOC_DMA2D_SUPPORTED && defined(LUNAROS_DISPLAY_DMA2D_AVAILABLE)
  // Encryption/ECC impose extra window alignment. Unsupported windows take
  // the CPU path without submitting a request that the driver must reject.
  if (dma_ && bytes >= 256 &&
      dma2d_check_transaction_alignment_constraint(src, stride, r.width, r.x, 16) &&
      dma2d_check_transaction_alignment_constraint(dst.pixels, dst.stride, r.width, x, 16)) {
    async_color_convert_request_t c{};
    c.src_buffer = src; c.src_stride = stride; c.src_height = height;
    c.src_x = r.x; c.src_y = r.y;
    c.dst_buffer = dst.pixels; c.dst_stride = dst.stride; c.dst_height = dst.height;
    c.dst_x = x; c.dst_y = y; c.copy_width = r.width; c.copy_height = r.height;
    c.src_color_format = ESP_COLOR_FOURCC_RGB16; c.dst_color_format = ESP_COLOR_FOURCC_RGB16;
    // The driver synchronizes partial cache lines and uses a static semaphore.
    if (esp_color_convert_blocking(static_cast<async_color_convert_handle_t>(dma_), &c, -1) == ESP_OK) {
      stats_.dma2d_bytes += bytes; return true;
    }
    ++stats_.hardware_failures;
  }
#endif
  for (int row = 0; row < r.height; ++row)
    std::memcpy(dst.pixels + size_t(y + row) * dst.stride + x,
                src + size_t(r.y + row) * stride + r.x, size_t(r.width) * 2);
  stats_.cpu_copy_bytes += bytes;
  return true;
}
bool PixelAccelerator::blend(PixelBuffer565 dst, int x, int y, const uint8_t *bgra,
                            int width, int height) {
  if (!dst.pixels || !bgra || !fits(x, y, width, height, dst.stride, dst.height) ||
      !fits(0, 0, width, height, width, height)) return false;
  const uint64_t pixels = uint64_t(width) * height;
#if defined(ESP_PLATFORM) && SOC_PPA_SUPPORTED && defined(LUNAROS_DISPLAY_PPA_AVAILABLE)
  const size_t size = size_t(dst.stride) * dst.height * 2;
  if (blend_ && pixels >= 128 && !(reinterpret_cast<uintptr_t>(dst.pixels) % 64) &&
      !(size % 64) && !(reinterpret_cast<uintptr_t>(bgra) % 64) &&
      size <= UINT32_MAX && dst.stride <= 4095 && dst.height <= 4095) {
    ppa_blend_oper_config_t c{};
    c.in_bg.buffer = dst.pixels; c.in_bg.pic_w = dst.stride; c.in_bg.pic_h = dst.height;
    c.in_bg.block_w = width; c.in_bg.block_h = height;
    c.in_bg.block_offset_x = x; c.in_bg.block_offset_y = y;
    c.in_bg.blend_cm = PPA_BLEND_COLOR_MODE_RGB565;
    c.in_fg.buffer = bgra; c.in_fg.pic_w = width; c.in_fg.pic_h = height;
    c.in_fg.block_w = width; c.in_fg.block_h = height;
    c.in_fg.blend_cm = PPA_BLEND_COLOR_MODE_ARGB8888;
    c.out.buffer = dst.pixels; c.out.buffer_size = size;
    c.out.pic_w = dst.stride; c.out.pic_h = dst.height;
    c.out.block_offset_x = x; c.out.block_offset_y = y;
    c.out.blend_cm = PPA_BLEND_COLOR_MODE_RGB565;
    c.bg_alpha_update_mode = PPA_ALPHA_FIX_VALUE; c.bg_alpha_fix_val = 255;
    c.fg_alpha_update_mode = PPA_ALPHA_NO_CHANGE;
    c.mode = PPA_TRANS_MODE_BLOCKING;
    if (ppa_do_blend(static_cast<ppa_client_handle_t>(blend_), &c) == ESP_OK) {
      stats_.ppa_blend_pixels += pixels; return true;
    }
    ++stats_.hardware_failures;
  }
#endif
  for (int row = 0; row < height; ++row) for (int column = 0; column < width; ++column) {
    auto &pixel = dst.pixels[size_t(y + row) * dst.stride + x + column];
    const auto *p = bgra + (size_t(row) * width + column) * 4;
    const unsigned a = p[3];
    if (!a) continue;
    unsigned r = (pixel >> 11) & 31, g = (pixel >> 5) & 63, b = pixel & 31;
    r = r * 255 / 31; g = g * 255 / 63; b = b * 255 / 31;
    r = (p[2] * a + r * (255 - a)) / 255;
    g = (p[1] * a + g * (255 - a)) / 255;
    b = (p[0] * a + b * (255 - a)) / 255;
    pixel = uint16_t((r >> 3) << 11 | (g >> 2) << 5 | (b >> 3));
  }
  stats_.cpu_blend_pixels += pixels;
  return true;
}
} // namespace lunaredu::display
