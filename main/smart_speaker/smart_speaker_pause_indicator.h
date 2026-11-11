#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace smart_speaker {
// Remaining budget, rather than a full extra frame after drawing.
inline uint32_t display_frame_wait_ms(uint32_t work_us, uint32_t period_ms) {
  return work_us < period_ms * 1000 ? (period_ms * 1000 - work_us) / 1000 : 0;
}

class VolumeIndicator {
 public:
  float update(uint32_t now, int volume, int maximum) {
    const float target = std::clamp(volume / static_cast<float>(std::max(maximum, 1)), 0.0f, 1.0f);
    if (!initialized_) { initialized_ = true; level_ = target; }
    const float dt = std::min<uint32_t>(now - previous_, 64) * .001f;
    previous_ = now;
    level_ += (target - level_) * (1 - std::exp(-dt * 25));
    if (std::abs(level_ - target) < .0005f) level_ = target;
    return level_;
  }
 private:
  bool initialized_ = false;
  uint32_t previous_ = 0;
  float level_ = 0;
};
} // namespace smart_speaker
