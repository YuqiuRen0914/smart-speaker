#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
namespace lunaredu::audio {
// Mono PCM16 adapter for Espressif Audio Effects. One owner must serialize
// configure/reset/process; the official handle retains history between blocks.
// Rate changes require distinct input/output buffers. Equal rates allow overlap.
class PcmRateConverter {
 public:
  PcmRateConverter() = default;
  ~PcmRateConverter();
  PcmRateConverter(const PcmRateConverter &) = delete;
  PcmRateConverter &operator=(const PcmRateConverter &) = delete;
  bool configure(unsigned input_rate, unsigned output_rate);
  bool reset();
  void close();
  size_t maximum_output_samples(size_t input_samples) const;
  bool process(const int16_t *input, size_t count, int16_t *output,
               size_t capacity, size_t &written);
 private:
  void *handle_ = nullptr;
  unsigned input_rate_ = 0, output_rate_ = 0;
  std::unique_ptr<int16_t[]> scratch_;
  size_t capacity_ = 0;
};

}
