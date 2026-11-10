#pragma once
#include "lunaredu_device_rgb565_transfer.h"
#include <array>

namespace lunaredu::device {
// Device-internal transport seam. send() borrows buffers in FIFO order until
// wait_one() retires the oldest one, or drain() retires all borrowed work.
// Window is set once; subsequent sends continue the same RAM write command.
struct Rgb565TransferPort {
  void *context = nullptr;
  void *(*allocate)(size_t) = nullptr;
  void (*release)(void *) = nullptr;
  bool (*window)(void *, uint16_t, uint16_t, uint16_t, uint16_t) = nullptr;
  bool (*send)(void *, const void *, size_t, bool first) = nullptr;
  bool (*drain)(void *) = nullptr;
  uint64_t (*now_us)() = nullptr;
  bool (*wait_one)(void *) = nullptr; // Optional; null keeps batch-drain fallback.
  bool (*profiling_enabled)()=nullptr;
  uint32_t (*task_runtime_us)()=nullptr;
};

class Rgb565Transfer {
 public:
  static constexpr size_t kBufferBytes = kRgb565TransferBufferBytes;
  bool setup(Rgb565TransferPort port);
  // Always completes borrowed source reads and DMA before returning success.
  // Failed drains quarantine the buffers; shutdown() must drain successfully
  // before releasing them. A single-buffer fallback stays chunked.
  bool draw(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
            const uint16_t *pixels, uint16_t stride, bool swap_bytes);
  bool shutdown();
  Rgb565TransferStats stats() const { return stats_; }
 private:
  bool drain();
  Rgb565TransferPort port_{};
  std::array<uint16_t *, 2> buffers_{};
  // One owner and lifetime for both DMA lanes. Split allocations remain a
  // fallback when a contiguous pair is unavailable; never free interior views.
  void *paired_allocation_ = nullptr;
  Rgb565TransferStats stats_{};
  unsigned pending_ = 0;
};
} // namespace lunaredu::device
