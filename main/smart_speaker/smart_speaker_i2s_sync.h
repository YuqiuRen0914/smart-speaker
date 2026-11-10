#pragma once

#include <cstdint>

namespace smart_speaker {

enum class I2sCompletionFault : uint8_t {
  kNone,
  kLeftStalled,
  kRightStalled,
};

// The two I2S controllers are independent masters. Their TX EOF callbacks are
// therefore not delivered in lockstep, and auto-clear keeps those callbacks
// running even while zero samples are being emitted. Absolute callback totals
// are telemetry, not a valid phase/synchronization invariant.
//
// This monitor only declares a fault when one controller makes no DMA progress
// for two complete observation windows while the peer continues to advance.
class I2sCompletionMonitor {
 public:
  static constexpr uint32_t kObservationWindowMs = 50;
  static constexpr uint64_t kMinimumPeerProgressFrames = 128U * 4U;
  static constexpr uint8_t kRequiredStallWindows = 2;

  void reset(uint64_t completed_left, uint64_t completed_right,
             uint32_t now_ms) {
    previous_left_ = completed_left;
    previous_right_ = completed_right;
    previous_ms_ = now_ms;
    left_stall_windows_ = 0;
    right_stall_windows_ = 0;
    initialized_ = true;
  }

  I2sCompletionFault observe(uint64_t completed_left,
                             uint64_t completed_right, uint32_t now_ms,
                             bool audio_active) {
    if (!initialized_ || !audio_active) {
      reset(completed_left, completed_right, now_ms);
      return I2sCompletionFault::kNone;
    }
    if (now_ms - previous_ms_ < kObservationWindowMs)
      return I2sCompletionFault::kNone;

    const uint64_t left_progress = completed_left - previous_left_;
    const uint64_t right_progress = completed_right - previous_right_;
    previous_left_ = completed_left;
    previous_right_ = completed_right;
    previous_ms_ = now_ms;

    const bool left_stalled =
        left_progress == 0 &&
        right_progress >= kMinimumPeerProgressFrames;
    const bool right_stalled =
        right_progress == 0 &&
        left_progress >= kMinimumPeerProgressFrames;
    left_stall_windows_ =
        left_stalled ? static_cast<uint8_t>(left_stall_windows_ + 1) : 0;
    right_stall_windows_ =
        right_stalled ? static_cast<uint8_t>(right_stall_windows_ + 1) : 0;

    if (left_stall_windows_ >= kRequiredStallWindows)
      return I2sCompletionFault::kLeftStalled;
    if (right_stall_windows_ >= kRequiredStallWindows)
      return I2sCompletionFault::kRightStalled;
    return I2sCompletionFault::kNone;
  }

 private:
  uint64_t previous_left_ = 0;
  uint64_t previous_right_ = 0;
  uint32_t previous_ms_ = 0;
  uint8_t left_stall_windows_ = 0;
  uint8_t right_stall_windows_ = 0;
  bool initialized_ = false;
};

}  // namespace smart_speaker
