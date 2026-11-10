#pragma once
#include <algorithm>
#include <cstdint>

namespace lunaredu::lunaros {
// Completion-based, allocation-free backoff. A failed attempt needs actual
// idle time after it finishes; its own blocking duration is not the cooldown.
// The owner serializes access and chooses which requests may bypass it.
class RetrySchedule {
public:
  constexpr RetrySchedule(uint32_t initial_ms, uint32_t maximum_ms)
      : initial_(std::clamp<uint32_t>(initial_ms, 1, INT32_MAX)),
        maximum_(std::clamp<uint32_t>(maximum_ms, initial_, INT32_MAX)) {}
  void failed(uint32_t completed_at_ms) {
    delay_ = delay_ == 0 ? initial_ :
        static_cast<uint32_t>(std::min<uint64_t>(uint64_t(delay_) * 2, maximum_));
    due_ = completed_at_ms + delay_;
    waiting_ = true;
  }
  void reset() { delay_ = due_ = 0; waiting_ = false; }
  bool ready(uint32_t now_ms) const {
    return !waiting_ || static_cast<int32_t>(now_ms - due_) >= 0;
  }
  uint32_t remaining_ms(uint32_t now_ms) const { return ready(now_ms) ? 0 : due_ - now_ms; }
private:
  uint32_t initial_, maximum_, delay_ = 0, due_ = 0;
  bool waiting_ = false;
};
} // namespace lunaredu::lunaros
