#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace smart_speaker {
inline bool show_pause_for_volume(int volume, bool playing, bool paused, bool selected) {
  return volume == 0 || (!playing && (paused || selected));
}
struct IndicatorStroke {
  float x1, y1, x2, y2, fill;
};

// A 112x4 track folds into two 4x16 pause strokes in a fixed 116x24 footer.
// Keep the same geometry throughout; neither endpoint cross-fades to an icon.
inline std::array<IndicatorStroke, 2> indicator_strokes(float morph, float level) {
  morph = std::clamp(morph, 0.0f, 1.0f);
  level = std::clamp(level, 0.0f, 1.0f);
  const float rest = 1-morph;
  const float length = 12 + 42*rest*rest*rest;
  const float center = 5 + 22*rest;
  const float angle = morph*1.57079632679f;
  const float dx = std::cos(angle)*length*.5f;
  const float dy = std::sin(angle)*length*.5f;
  std::array<IndicatorStroke, 2> strokes{};
  for (size_t i=0; i<strokes.size(); ++i) {
    const float side = i == 0 ? -1.0f : 1.0f;
    const float fill = std::clamp(level*2-static_cast<float>(i), 0.0f, 1.0f);
    strokes[i] = {side*center-dx, side*dy, side*center+dx, -side*dy,
                  fill+(1-fill)*morph};
  }
  return strokes;
}

// Critically damped motion preserves both position and velocity on reversal.
class PauseIndicator {
 public:
  float update(uint32_t now, bool paused) {
    if (!initialized_) { initialized_=true; previous_=now; }
    if (was_paused_ && !paused) { resumed_=true; resumed_ms_=now; }
    was_paused_=paused;
    dt_ = std::min<uint32_t>(now-previous_,64)*.001f;
    previous_=now;
    const float target = paused ? 1.0f : 0.0f;
    constexpr float frequency = 22;
    const float offset = amount_-target;
    const float motion = (velocity_+frequency*offset)*dt_;
    const float decay = std::exp(-frequency*dt_);
    amount_ = target+(offset+motion)*decay;
    velocity_ = (velocity_-frequency*motion)*decay;
    if (std::abs(amount_-target) < .001f && std::abs(velocity_) < .02f) {
      amount_=target;
      velocity_=0;
    }
    return std::clamp(amount_,0.0f,1.0f);
  }
  float volume_level(int volume, int maximum) {
    const float target = std::clamp(volume/static_cast<float>(std::max(maximum,1)),0.0f,1.0f);
    if (!level_initialized_) { level_initialized_=true; level_=target; }
    // Retain the last audible fill while folding at zero. This avoids the
    // gold bar first emptying and then reappearing as gold pause strokes.
    if (volume > 0) {
      level_ += (target-level_)*(1-std::exp(-dt_*25));
      if (std::abs(level_-target)<.0005f) level_=target;
    }
    return level_;
  }
  float resume_opacity(uint32_t now) const {
    if (!resumed_ || now-resumed_ms_ >= 1500) return 0;
    return std::min((1500.0f-uint32_t(now-resumed_ms_))/250.0f,1.0f);
  }
 private:
  bool was_paused_=false, resumed_=false;
  uint32_t resumed_ms_=0;
  bool initialized_=false;
  uint32_t previous_=0;
  float amount_=0;
  float velocity_=0, dt_=0, level_=0;
  bool level_initialized_=false;
};
} // namespace smart_speaker
