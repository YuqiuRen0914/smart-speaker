#pragma once
#include "display/frame.h"
#include "display/backend.h"
#include <array>
#include <mutex>

namespace lunaredu::display {
// Associates application-owned content revisions with screen-owned leases.
// Preparation is not proof of visibility. Complete only after a whole frame
// succeeds, or the screen verifies it against its acknowledged baseline.
template<size_t Slots = 2> class PresentationCompletion {
 public:
  bool prepare(FrameCanvas canvas, uint64_t revision) {
    if (!canvas.pixels || !canvas.issuer || !canvas.generation || !revision ||
        !canvas.width || !canvas.height || canvas.stride < canvas.width || canvas.slot >= Slots) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    entries_[canvas.slot] = {canvas, revision};
    return true;
  }
  uint64_t complete(FrameCanvas canvas) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (canvas.slot >= Slots) return 0;
    const auto &entry = entries_[canvas.slot];
    const auto &expected = entry.canvas;
    return canvas.pixels && canvas.issuer && canvas.generation &&
        canvas.pixels == expected.pixels && canvas.issuer == expected.issuer &&
        canvas.generation == expected.generation && canvas.width == expected.width &&
        canvas.height == expected.height && canvas.stride == expected.stride ? entry.revision : 0;
  }
  uint64_t complete(ScreenFrameView frame) const {
    if (frame.width <= 0 || frame.height <= 0 || frame.stride < frame.width ||
        frame.width > UINT16_MAX || frame.height > UINT16_MAX || frame.stride > UINT16_MAX ||
        !frame.pixels || !frame.issuer || !frame.generation || frame.slot >= Slots) return 0;
    std::lock_guard<std::mutex> lock(mutex_);
    const auto &entry=entries_[frame.slot];const auto &canvas=entry.canvas;
    if(canvas.issuer!=frame.issuer || canvas.generation!=frame.generation || canvas.stride!=frame.stride) return 0;
    const auto base=reinterpret_cast<uintptr_t>(frame.pixels),address=reinterpret_cast<uintptr_t>(canvas.pixels);
    if(address<base || (address-base)%sizeof(uint16_t))return 0;
    const auto offset=(address-base)/sizeof(uint16_t),row=offset/size_t(frame.stride),column=offset%size_t(frame.stride);
    return row<size_t(frame.height) && column<size_t(frame.width) &&
        canvas.height<=size_t(frame.height)-row && canvas.width<=size_t(frame.width)-column ? entry.revision : 0;
  }
  void clear() { std::lock_guard<std::mutex> lock(mutex_); entries_ = {}; }
 private:
  struct Entry { FrameCanvas canvas{}; uint64_t revision = 0; };
  mutable std::mutex mutex_;
  std::array<Entry, Slots> entries_{};
};
}
