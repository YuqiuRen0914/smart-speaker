#include "lunaros_audio_resampler.h"
#include <cstring>
#include <limits>
#include <algorithm>
#include <new>
#include <esp_ae_rate_cvt.h>
namespace lunaredu::audio {
namespace {
bool supported_rate(unsigned rate) {
  return rate >= 8000 && rate <= 96000 &&
         (rate % 4000 == 0 || rate % 11025 == 0);
}
}
PcmRateConverter::~PcmRateConverter() {
  close();
}
void PcmRateConverter::close(){
  if (handle_) esp_ae_rate_cvt_close(handle_);
  handle_=nullptr;input_rate_=output_rate_=0;capacity_=0;scratch_.reset();
}
bool PcmRateConverter::configure(unsigned input, unsigned output) {
  if (!supported_rate(input) || !supported_rate(output)) return false;
  void *next = nullptr;
  std::unique_ptr<int16_t[]> scratch;
  uint32_t capacity = 0;
  if (input != output) {
    esp_ae_rate_cvt_cfg_t config{};
    config.src_rate = input;
    config.dest_rate = output;
    config.channel = 1;
    config.bits_per_sample = 16;
    config.complexity = 2;
    config.perf_type = ESP_AE_RATE_CVT_PERF_TYPE_MEMORY;
    if (esp_ae_rate_cvt_open(&config, &next) != ESP_AE_ERR_OK) return false;
    if (esp_ae_rate_cvt_get_max_out_sample_num(next, 1, &capacity) != ESP_AE_ERR_OK ||
        !capacity) {
      esp_ae_rate_cvt_close(next); return false;
    }
    scratch.reset(new (std::nothrow) int16_t[capacity]);
    if (!scratch) { esp_ae_rate_cvt_close(next); return false; }
  }
  if (handle_) esp_ae_rate_cvt_close(handle_);
  handle_ = next;
  input_rate_ = input;
  output_rate_ = output;
  scratch_ = std::move(scratch);
  capacity_ = capacity;
  return true;
}
bool PcmRateConverter::reset() {
  return input_rate_ && (!handle_ || esp_ae_rate_cvt_reset(handle_) == ESP_AE_ERR_OK);
}
size_t PcmRateConverter::maximum_output_samples(size_t count) const {
  if (!input_rate_ || count > std::numeric_limits<uint32_t>::max()) return 0;
  const auto maximum=(uint64_t(count) * output_rate_ + input_rate_ - 1) / input_rate_;
  return maximum > std::numeric_limits<uint32_t>::max() ? 0 : size_t(maximum);
}
bool PcmRateConverter::process(const int16_t *input, size_t count, int16_t *output,
                               size_t capacity, size_t &written) {
  written = 0;
  if (!input_rate_) return false;
  if (!count) return true;
  const size_t maximum = maximum_output_samples(count);
  if (!input || !output || !maximum || capacity < maximum ||
      capacity > std::numeric_limits<uint32_t>::max()) return false;
  if (!handle_) {
    std::memmove(output, input, count * sizeof(int16_t));
    written = count;
    return true;
  }
  const auto source=reinterpret_cast<uintptr_t>(input);
  const auto destination=reinterpret_cast<uintptr_t>(output);
  if ((destination>=source && destination-source<count*sizeof(int16_t)) ||
      (source>=destination && source-destination<maximum*sizeof(int16_t))) return false;
  // 1.4.3 retains frame-size-dependent state: changing its input block size can
  // return excess samples (480 then 120 @24->16 kHz). Keep a fixed one-sample
  // quantum at the vendor boundary while exposing arbitrary caller blocks.
  // The vendor's ~128-sample capacity margin is allocated only at configure.
  for (size_t i = 0; i < count; ++i) {
    int16_t sample = input[i];
    uint32_t produced = uint32_t(capacity_);
    if (esp_ae_rate_cvt_process(handle_, &sample, 1, scratch_.get(), &produced) != ESP_AE_ERR_OK ||
        produced > capacity_ || produced > capacity - written) {
      written = 0; return false;
    }
    std::copy_n(scratch_.get(), produced, output + written);
    written += produced;
  }
  return true;
}
}
