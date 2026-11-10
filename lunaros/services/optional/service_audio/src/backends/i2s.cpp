#include "pcm_backend.h"
#include <new>
#include "lunaredu_device_i2s_audio_device.h"
#include "realtime/realtime_pcm_backend.h"

namespace lunaredu::audio {
namespace {
class I2sBackend final : public PcmBackend {
  lunaredu::device::I2sPcmAudioDevice device_;
public:
  explicit I2sBackend(lunaredu::device::I2sPcmAudioDeviceConfig config) : device_(config) {}
  bool initialize() { return device_.initialize(); }
  esp_err_t initialization_error() const { return device_.initialization_error(); }
  int input_sample_rate() const override { return device_.input_sample_rate(); }
  int output_sample_rate() const override { return device_.output_sample_rate(); }
  bool enable_input(bool enabled) override { return device_.enable_input(enabled); }
  bool enable_output(bool enabled) override { return device_.enable_output(enabled); }
  bool release_idle_resources() override { return device_.release_idle_resources(); }
  bool shutdown(std::string &error) override {
    if(!device_.enable_input(false)||!device_.enable_output(false)||
       !device_.release_idle_resources()) {
      error="i2s_channels_retirement_pending";return false;
    }
    error.clear();return true;
  }
  bool configure_output_sample_rate(uint32_t rate) override { return device_.configure_output_sample_rate(rate); }
  bool read_input(int16_t *data, size_t count, uint32_t timeout) override { return device_.read_input(data, count, timeout); }
  bool has_duplex_reference()const override{return device_.has_input_reference();}
  bool read_duplex(int16_t *microphone,int16_t *reference,size_t count,uint32_t timeout,PcmDuplexObservation &out)override {
    device::I2sPcmPairedRead read;out={};
    if(!device_.read_paired_input(microphone,reference,count,timeout,read))return false;
    out.synchronous_reference=true;out.sample_rate=read.sample_rate;out.reference_samples=read.reference_samples;
    out.capture_generation=read.generation;out.first_sample=read.first_sample;
    out.reference_delay_known=read.delay_known;out.reference_delay_samples=read.delay_samples;
    // Positions describe SDK-consumed PCM, without hardware overflow/ADC time proof.
    out.sample_clock_valid=false;out.monotonic_time_valid=false;return true;
  }
  bool write_output(const int16_t *data, size_t count, uint16_t gain_q15,
                    uint32_t timeout) override {
    return device_.write_output_with_gain(data, count, gain_q15, timeout);
  }
};
}
std::unique_ptr<PcmBackend> create_pcm_backend(
    lunaredu::lunaros::KernelRuntime *runtime,
    const char *owner_service_id, std::string &error) {
  if (runtime == nullptr || owner_service_id == nullptr ||
      owner_service_id[0] == '\0') {
    error = "audio_task_runtime_missing";
    return nullptr;
  }
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
  // Configuration and BSP ownership are ready; native channels/DMA are
  // allocated by the first actual input/output demand, not service discovery.
  return create_realtime_pcm_backend(std::move(backend), *runtime,
                                     owner_service_id, error);
}
} // namespace lunaredu::audio
