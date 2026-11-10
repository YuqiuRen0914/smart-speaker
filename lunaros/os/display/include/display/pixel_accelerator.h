#pragma once
#include <cstddef>
#include <cstdint>
#include "display/screen_host.h"

namespace lunaredu::display {
struct PixelBuffer565 {
  uint16_t *pixels = nullptr;
  int stride = 0, height = 0;
};
struct PixelAccelerationStats {
  bool dma2d_ready = false, ppa_blend_ready = false;
  uint64_t dma2d_bytes = 0, cpu_copy_bytes = 0;
  uint64_t ppa_blend_pixels = 0, cpu_blend_pixels = 0;
  uint32_t hardware_failures = 0;
};
// One display owner serializes these operations and snapshot access. No tasks,
// frame allocations or per-frame heap allocations. initialize() opts into SoC
// engines once; unsupported platforms use the same software contract.
// Calls finish reading borrowed sources before returning, including HW DMA.
// Buffers must not overlap for copy; blend is explicitly in-place RGB565 over
// BGRA bytes (straight alpha), with an aligned, whole-picture destination.
class PixelAccelerator {
 public:
  PixelAccelerator() = default;
  ~PixelAccelerator();
  PixelAccelerator(const PixelAccelerator &) = delete;
  PixelAccelerator &operator=(const PixelAccelerator &) = delete;
  void initialize();
  bool copy(PixelBuffer565 dst, int x, int y, const uint16_t *src,
            int src_stride, int src_height, ScreenRegion source);
  bool blend(PixelBuffer565 dst, int x, int y, const uint8_t *bgra,
             int width, int height);
  PixelAccelerationStats snapshot() const { return stats_; }
 private:
  // Hardware handles are unused on host and SoCs without these engines.
  [[maybe_unused]] void *dma_ = nullptr, *blend_ = nullptr;
  bool initialized_ = false;
  PixelAccelerationStats stats_{};
};
} // namespace lunaredu::display
