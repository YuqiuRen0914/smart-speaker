#pragma once
#include <cstddef>
#include <cstdint>

namespace lunaredu::device {
inline constexpr size_t kRgb565TransferBufferBytes = 4096;
struct Rgb565TransferStats {
  uint8_t buffer_count = 0;
  uint32_t buffer_bytes = 0; // Per buffer, internal DMA RAM.
  // Diagnostic address of the single owning allocation, zero for split mode.
  uintptr_t allocation_start = 0;
  bool paired_allocation = false;
  uint32_t frames = 0, failures = 0, submitted = 0, completed = 0;
  uint32_t drains = 0;
  uint64_t bytes = 0, last_draw_us = 0, max_draw_us = 0;
  // Cumulative elapsed times; copy/send can overlap queued DMA on the wire.
  uint64_t window_us = 0, copy_us = 0, send_us = 0, drain_us = 0;
  uint32_t recycles = 0;
  uint64_t recycle_us = 0;
  // Diagnostic-only settled scheduler slices, distinct from elapsed DMA time.
  uint32_t profiled_frames=0;
  uint64_t window_task_us=0,copy_task_us=0,send_task_us=0,recycle_task_us=0,drain_task_us=0;
  bool rolling_reuse = false;
  bool faulted = false;
};
} // namespace lunaredu::device
