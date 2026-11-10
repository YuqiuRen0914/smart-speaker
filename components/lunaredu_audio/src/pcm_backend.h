#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace lunaredu::audio {
// Core PCM buffering and volume processing depend only on this backend contract.
class PcmBackend {
public:
  virtual ~PcmBackend() = default;
  virtual int input_sample_rate() const = 0;
  virtual int output_sample_rate() const = 0;
  virtual bool enable_input(bool) = 0;
  virtual bool enable_output(bool) = 0;
  virtual bool configure_output_sample_rate(uint32_t) = 0;
  virtual bool read_input(int16_t *, size_t, uint32_t) = 0;
  virtual bool write_output(const int16_t *, size_t, uint32_t) = 0;
};
std::unique_ptr<PcmBackend> create_pcm_backend(std::string &error);
} // namespace lunaredu::audio
