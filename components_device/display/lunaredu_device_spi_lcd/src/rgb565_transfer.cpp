#include "rgb565_transfer.h"
#include <algorithm>
#include <cstring>
#include <lvgl/draw/lv_draw_utils.h>

namespace lunaredu::device {
bool Rgb565Transfer::setup(Rgb565TransferPort port) {
  if (stats_.buffer_count) return !stats_.faulted;
  if (!port.allocate || !port.release || !port.window || !port.send ||
      !port.drain || !port.now_us) return false;
  port_ = port;
  stats_.rolling_reuse = port.wait_one != nullptr;
  paired_allocation_ = port.allocate(2 * kBufferBytes);
  if (paired_allocation_) {
    buffers_[0] = static_cast<uint16_t *>(paired_allocation_);
    buffers_[1] = buffers_[0] + kBufferBytes / sizeof(uint16_t);
    stats_.buffer_count = 2;
    stats_.buffer_bytes = kBufferBytes;
    stats_.paired_allocation = true;
    stats_.allocation_start = reinterpret_cast<uintptr_t>(paired_allocation_);
    return true;
  }
  buffers_[0] = static_cast<uint16_t *>(port.allocate(kBufferBytes));
  if (!buffers_[0]) return false;
  stats_.buffer_count = 1;
  stats_.buffer_bytes = kBufferBytes;
  buffers_[1] = static_cast<uint16_t *>(port.allocate(kBufferBytes));
  if (buffers_[1]) stats_.buffer_count = 2;
  return true;
}

bool Rgb565Transfer::drain() {
  ++stats_.drains;
  const uint64_t started = port_.now_us();
  const bool profiling=port_.profiling_enabled&&port_.task_runtime_us&&port_.profiling_enabled();
  const uint32_t task_started=profiling?port_.task_runtime_us():0;
  const bool ok = port_.drain(port_.context);
  stats_.drain_us += port_.now_us() - started;
  if(profiling)stats_.drain_task_us+=uint32_t(port_.task_runtime_us()-task_started);
  if (!ok) {
    stats_.faulted = true;
    return false;
  }
  stats_.completed += pending_;
  pending_ = 0;
  return true;
}

bool Rgb565Transfer::draw(uint16_t x, uint16_t y, uint16_t width,
                         uint16_t height, const uint16_t *pixels,
                         uint16_t stride, bool swap_bytes) {
  if (!pixels || !width || !height || stride < width ||
      !stats_.buffer_count || stats_.faulted) return false;
  const uint64_t started = port_.now_us();
  const bool profiling=port_.profiling_enabled&&port_.task_runtime_us&&port_.profiling_enabled();
  const auto task_clock=[&]{return profiling?port_.task_runtime_us():uint32_t(0);};
  const uint32_t window_task=task_clock();
  bool ok = port_.window(port_.context, x, y, width, height);
  stats_.window_us += port_.now_us() - started;
  if(profiling)stats_.window_task_us+=uint32_t(task_clock()-window_task);
  const size_t total = size_t{width} * height;
  size_t offset = 0;
  unsigned next_buffer = 0;
  while (ok && offset < total) {
    if (pending_ == stats_.buffer_count) {
      if (port_.wait_one) {
        const uint64_t waiting = port_.now_us();
        const uint32_t waiting_task=task_clock();
        const bool retired = port_.wait_one(port_.context);
        stats_.recycle_us += port_.now_us() - waiting;
        if(profiling)stats_.recycle_task_us+=uint32_t(task_clock()-waiting_task);
        if (!retired) { stats_.faulted = true; ok = false; break; }
        --pending_; ++stats_.completed; ++stats_.recycles;
      } else if (!drain()) { ok = false; break; }
    }
    auto *target = buffers_[next_buffer];
    const size_t count = std::min(kBufferBytes / 2, total - offset);
    const uint64_t copy_started = port_.now_us();
    const uint32_t copy_task=task_clock();
    const auto copy = [](uint16_t *destination,
                                  const uint16_t *source, size_t length) {
      std::memcpy(destination, source, length * sizeof(uint16_t));
    };
    if (stride == width) {
      copy(target, pixels + offset, count);
    } else {
      // Compute a row/column once per chunk, never once per pixel.
      size_t row = offset / width, column = offset % width, copied = 0;
      while (copied < count) {
        const size_t span = std::min(size_t{width} - column, count - copied);
        copy(target + copied, pixels + row * stride + column, span);
        copied += span; ++row; column = 0;
      }
    }
    // Convert the retired staging buffer, never the caller's frame.
    if(swap_bytes)lv_draw_rgb565_swap(target,uint32_t(count));
    stats_.copy_us += port_.now_us() - copy_started;
    if(profiling)stats_.copy_task_us+=uint32_t(task_clock()-copy_task);
    const uint64_t send_started = port_.now_us();
    const uint32_t send_task=task_clock();
    ok = port_.send(port_.context, target, count * 2, offset == 0);
    stats_.send_us += port_.now_us() - send_started;
    if(profiling)stats_.send_task_us+=uint32_t(task_clock()-send_task);
    if (ok) {
      ++pending_; ++stats_.submitted;
      stats_.bytes += count * 2;
      offset += count;
      next_buffer = (next_buffer + 1) % stats_.buffer_count;
    }
  }
  // Also drain after a send failure: its HAL may have accepted partial work.
  if (!stats_.faulted && !drain()) ok = false;
  stats_.last_draw_us = port_.now_us() - started;
  stats_.max_draw_us = std::max(stats_.max_draw_us, stats_.last_draw_us);
  if (ok) ++stats_.frames; else ++stats_.failures;
  if(profiling)++stats_.profiled_frames;
  return ok;
}

bool Rgb565Transfer::shutdown() {
  if (stats_.buffer_count && !drain()) return false;
  if (paired_allocation_) {
    port_.release(paired_allocation_);
    paired_allocation_ = nullptr;
    buffers_ = {};
  }
  for (auto &buffer : buffers_) {
    if (buffer) port_.release(buffer);
    buffer = nullptr;
  }
  stats_ = {}; pending_ = 0; port_ = {};
  return true;
}
} // namespace lunaredu::device
