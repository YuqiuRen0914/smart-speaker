#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

namespace lunaredu::pet::conversation::internal {

struct StandbyPcmStatistics {
  uint32_t peak = 0;
  uint32_t rms = 0;
  uint32_t raw_peak = 0;
  uint32_t raw_rms = 0;
  int32_t raw_mean = 0;
};

inline StandbyPcmStatistics standby_pcm_statistics(const int16_t *samples,
                                                   size_t count) {
  StandbyPcmStatistics result;
  if (samples == nullptr || count == 0) return result;
  int64_t sum = 0;
  uint64_t raw_square = 0;
  for (size_t n = 0; n < count; ++n) {
    const int32_t value = samples[n];
    sum += value;
    raw_square += static_cast<uint64_t>(value * value);
    result.raw_peak = std::max(result.raw_peak,
        static_cast<uint32_t>(std::abs(static_cast<int32_t>(value))));
  }
  result.raw_mean = static_cast<int32_t>(sum / static_cast<int64_t>(count));
  uint64_t centered_square = 0;
  for (size_t n = 0; n < count; ++n) {
    // Exactly the truncating mean and saturating DC removal used by the audio
    // path, without copying or modifying WakeNet's original PCM samples.
    const int32_t centered = std::clamp<int32_t>(
        static_cast<int32_t>(samples[n]) - result.raw_mean, -32768, 32767);
    result.peak = std::max(result.peak,
        static_cast<uint32_t>(std::abs(static_cast<int32_t>(centered))));
    centered_square += static_cast<uint64_t>(centered * centered);
  }
  // int16 squares are exact integers. Only the final division/sqrt needs FP;
  // retain the previous conversion order to preserve threshold decisions.
  result.raw_rms = static_cast<uint32_t>(std::sqrt(static_cast<double>(
      static_cast<long double>(raw_square) / static_cast<long double>(count))));
  result.rms = static_cast<uint32_t>(std::sqrt(static_cast<double>(
      static_cast<long double>(centered_square) / static_cast<long double>(count))));
  return result;
}

} // namespace lunaredu::pet::conversation::internal
