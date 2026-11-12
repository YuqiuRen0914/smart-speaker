#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace smart_speaker {
// Coalesced finite trigger; disabling consumes old counts and cancels pending
// hits. Reopening a page cannot play strikes made while it was hidden.
class WoodFishStrikeGate {
 public:
  bool poll(bool enabled, uint32_t count, uint32_t now) {
    if (!enabled) pending_ = false;
    else if (enabled_ && count != count_ && !pending_) {
      started_ = now;
      pending_ = true;
    }
    enabled_ = enabled;
    count_ = count;
    if (pending_ && uint32_t(now - started_) >= 65) {
      pending_ = false;
      return true;
    }
    return false;
  }
 private:
  bool enabled_ = false, pending_ = false;
  uint32_t count_ = 0, started_ = 0;
};

// Original finite modal synthesis: no sound file, heap, decoder or I2S owner.
// Consumed only by the existing output task at its current bus sample rate.
class WoodFishSound {
 public:
  void strike(uint32_t rate) {
    rate_ = std::clamp(rate, uint32_t(8000), uint32_t(48000));
    position_ = 0;
    for (int i = 0; i < 3; ++i) {
      const float frequencies[] = {720, 1237, 2131};
      const float decays[] = {.042f, .023f, .012f};
      const float angle = 6.283185307f * frequencies[i] / rate_;
      const float decay = std::exp(-1.0f / (rate_ * decays[i]));
      cosine_[i] = std::cos(angle) * decay;
      sine_[i] = std::sin(angle) * decay;
      real_[i] = 1; imaginary_[i] = 0;
    }
  }
  bool active() const { return position_ < rate_ / 5; }
  void cancel() { position_ = rate_; }
  int16_t next() {
    if (!active()) return 0;
    float sample = 0;
    constexpr float gains[] = {5500, 2300, 1200};
    for (int i = 0; i < 3; ++i) {
      const float real = real_[i] * cosine_[i] - imaginary_[i] * sine_[i];
      imaginary_[i] = real_[i] * sine_[i] + imaginary_[i] * cosine_[i];
      real_[i] = real;
      sample += imaginary_[i] * gains[i];
    }
    const float attack = std::min(1.0f, float(position_) / (rate_ * .0015f));
    const float release = std::min(1.0f, float(rate_ / 5 - position_) / (rate_ * .01f));
    ++position_;
    return static_cast<int16_t>(sample * attack * release);
  }
 private:
  uint32_t rate_ = 44100, position_ = 44100;
  float real_[3]{}, imaginary_[3]{}, cosine_[3]{}, sine_[3]{};
};
}
