#include "pcm_backend.h"
#include <new>
#include "lunaredu_device_i2s_audio_device.h"

namespace lunaredu::audio {
namespace {
class I2sBackend final : public PcmBackend {
  lunaredu::device::I2sPcmAudioDevice device_;
public:
  explicit I2sBackend(lunaredu::device::I2sPcmAudioDeviceConfig config) : device_(config) {}
  bool initialize() { return device_.initialize(); }
  int input_sample_rate() const override { return device_.input_sample_rate(); }
  int output_sample_rate() const override { return device_.output_sample_rate(); }
  bool enable_input(bool enabled) override { return device_.enable_input(enabled); }
  bool enable_output(bool enabled) override { return device_.enable_output(enabled); }
  bool configure_output_sample_rate(uint32_t rate) override { return device_.configure_output_sample_rate(rate); }
  bool read_input(int16_t *data, size_t count, uint32_t timeout) override { return device_.read_input(data, count, timeout); }
  bool write_output(const int16_t *data, size_t count, uint32_t timeout) override { return device_.write_output(data, count, timeout); }
};
}
std::unique_ptr<PcmBackend> create_pcm_backend(std::string &error) {
  lunaredu::device::I2sPcmAudioDeviceConfig config;
  const esp_err_t result = lunaredu::device::i2s_pcm_audio_device_prepare_from_kconfig(&config);
  if (result != ESP_OK) {
    error = std::string("prepare_i2s_audio_failed:") + esp_err_to_name(result);
    return nullptr;
  }
  std::unique_ptr<I2sBackend> backend(new (std::nothrow) I2sBackend(config));
  if (!backend) {
    lunaredu::device::device_i2s_audio_release(&config.i2s_audio_claim);
    error = "audio_backend_allocation_failed";
    return nullptr;
  }
  if (!backend->initialize()) { error = "initialize_i2s_audio_failed"; return nullptr; }
  error.clear();
  return backend;
}
} // namespace lunaredu::audio
