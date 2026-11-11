#pragma once

#include <algorithm>
#include <cstdint>

namespace smart_speaker {

// Streaming area averaging preserves duration and DC across arbitrary I2S DMA
// blocks. At the 8–48 kHz bus rates, one input produces at most two 16 kHz samples.
class MicrophoneResampler {
 public:
  void reset(uint32_t input_rate) {
    rate_ = input_rate;
    filled_ = 0;
    sum_ = 0;
  }
  unsigned push(int16_t sample, int16_t *output) {
    if (!rate_) return 0;
    unsigned produced = 0;
    uint32_t remaining = 16000;
    while (remaining) {
      const uint32_t weight = std::min(remaining, rate_ - filled_);
      sum_ += int64_t(sample) * weight;
      filled_ += weight;
      remaining -= weight;
      if (filled_ == rate_) {
        output[produced++] = static_cast<int16_t>(sum_ / rate_);
        sum_ = 0;
        filled_ = 0;
      }
    }
    return produced;
  }
 private:
  uint32_t rate_ = 0, filled_ = 0;
  int64_t sum_ = 0;
};

} // namespace smart_speaker
