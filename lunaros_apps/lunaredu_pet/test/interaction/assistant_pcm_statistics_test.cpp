#include "interaction/voice/assistant_pcm_statistics.h"
#include <cassert>
#include <vector>

using lunaredu::pet::conversation::internal::StandbyPcmStatistics;

// Reference is the old copy + DC-removal + floating-point accumulation path.
StandbyPcmStatistics reference(std::vector<int16_t> samples) {
  StandbyPcmStatistics result;
  if (samples.empty()) return result;
  int64_t sum = 0;
  long double square = 0;
  for (auto sample : samples) {
    sum += sample;
    result.raw_peak = std::max(result.raw_peak, static_cast<uint32_t>(std::abs(sample)));
    const long double value = sample;
    square += value * value;
  }
  result.raw_mean = static_cast<int32_t>(sum / static_cast<int64_t>(samples.size()));
  result.raw_rms = static_cast<uint32_t>(std::sqrt(static_cast<double>(square / samples.size())));
  for (auto &sample : samples)
    sample = static_cast<int16_t>(std::clamp<int32_t>(sample - result.raw_mean, -32768, 32767));
  square = 0;
  for (auto sample : samples) {
    result.peak = std::max(result.peak, static_cast<uint32_t>(std::abs(sample)));
    const long double value = sample;
    square += value * value;
  }
  result.rms = static_cast<uint32_t>(std::sqrt(static_cast<double>(square / samples.size())));
  return result;
}

void check(const std::vector<int16_t> &samples) {
  const auto original = samples;
  const auto expected = reference(samples);
  const auto actual = lunaredu::pet::conversation::internal::standby_pcm_statistics(samples.data(), samples.size());
  assert(actual.raw_mean == expected.raw_mean && actual.raw_peak == expected.raw_peak);
  assert(actual.raw_rms == expected.raw_rms && actual.peak == expected.peak && actual.rms == expected.rms);
  assert(samples == original);
}

int main() {
  check({}); check({-32768}); check({32767}); check({-32768, 32767});
  for (int value : {-32768, -1000, -1, 0, 1, 1000, 32767}) {
    std::vector<int16_t> pcm(160, static_cast<int16_t>(value));
    check(pcm);
    pcm[0] = -32768; pcm[1] = 32767;
    check(pcm);
  }
  uint32_t random = 1;
  for (size_t round = 0; round < 10000; ++round) {
    std::vector<int16_t> pcm(round % 3 == 0 ? 960 : 160);
    for (auto &sample : pcm) {
      random = random * 1664525U + 1013904223U;
      sample = static_cast<int16_t>((random >> 16) - 32768);
    }
    check(pcm);
  }
}
