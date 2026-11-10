#pragma once
#include <cstdint>
namespace lunaredu::device {
// Completion timestamps measure successful reads, never configured frequency
// or repeated UI observations. Restart the window for each demand session.
struct Mpu6050SamplingTiming {
  uint32_t samples = 0;
  int64_t first_us = 0, last_us = 0;
  uint32_t minimum_interval_us = 0, maximum_interval_us = 0;
  void record(int64_t now_us) {
    if (samples && now_us > last_us) {
      const auto interval = static_cast<uint32_t>(now_us - last_us);
      if (!minimum_interval_us || interval < minimum_interval_us)
        minimum_interval_us = interval;
      if (interval > maximum_interval_us) maximum_interval_us = interval;
    }
    if (!samples) first_us = now_us;
    last_us = now_us;
    ++samples;
  }
  double rate_hz() const {
    return samples > 1 && last_us > first_us
        ? (samples - 1) * 1000000.0 / (last_us - first_us) : 0;
  }
};
}
