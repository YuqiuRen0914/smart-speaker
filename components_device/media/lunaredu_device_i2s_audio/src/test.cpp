#include "lunaredu_device_i2s_audio_test.h"

#include "sdkconfig.h"
#if CONFIG_LUNAREDU_DEVICE_ENABLE_AUDIO && CONFIG_LUNAREDU_DEVICE_I2S_AUDIO
#include "lunaredu_lunaros/esp_idf/device_test.h"
#include <esp_log.h>
#include <cstdint>
#include "lunaredu_device_i2s_audio_device.h"
#include <algorithm>
#include <cmath>
#include <new>

namespace lunaredu::device::i2s_audio {
namespace {
constexpr const char *kTag = "test_i2s_audio";
I2sPcmAudioDevice *audio = nullptr;
I2sPcmAudioDeviceConfig prepared;
int16_t samples[2048] = {};
uint32_t received = 0, read_errors = 0, write_errors = 0;
int peak = 0;
float oscillator = 0;
[[maybe_unused]] uint32_t reported_ms = 0;
[[maybe_unused]] uint32_t phase = 0;

esp_err_t initialize(const void *opaque) {
  [[maybe_unused]] const auto &config = *static_cast<const TestConfig *>(opaque);
  reported_ms = 0;
  phase = 0;
  received = read_errors = write_errors = 0; peak = 0; oscillator = 0;
  if (config.input_sample_rate < 8000 || config.input_sample_rate > 96000 || config.output_sample_rate < 8000 || config.output_sample_rate > 96000) return ESP_ERR_INVALID_ARG;
  const auto result = i2s_pcm_audio_device_prepare_from_i2s_audio(config, &prepared);
  if (result != ESP_OK) return result;
  audio = new (std::nothrow) I2sPcmAudioDevice(prepared);
  if (!audio) return ESP_ERR_NO_MEM;
  prepared.i2s_audio_claim = {};  // release responsibility moves to the device
  if (!audio->initialize()) return ESP_FAIL;
  if (prepared.input.enabled && !audio->enable_input(true)) return ESP_FAIL;
  if (prepared.output.enabled && !audio->enable_output(true)) return ESP_FAIL;
  return ESP_OK;
}

void tick([[maybe_unused]] uint32_t elapsed_ms) {
  if (prepared.input.enabled) {
    const size_t count = static_cast<size_t>(prepared.input.sample_rate / 100) * prepared.input.channels;
    if (audio->read_input(samples, count, 10)) {
      received += static_cast<uint32_t>(count);
      for (size_t i = 0; i < count; ++i) { const int v = samples[i]; peak = std::max(peak, v < 0 ? -v : v); }
    } else ++read_errors;
  }
  if (prepared.output.enabled) {
    const size_t frames = prepared.output.sample_rate / 100;
    const size_t channels = static_cast<size_t>(prepared.output.channels);
    for (size_t i = 0; i < frames; ++i) {
      const int16_t value = elapsed_ms % 2000 < 200 ? static_cast<int16_t>(800 * std::sin(oscillator)) : 0;
      oscillator += 6.2831853f * 440.0f / static_cast<float>(prepared.output.sample_rate);
      if (oscillator >= 6.2831853f) oscillator -= 6.2831853f;
      for (size_t ch = 0; ch < channels; ++ch) samples[i * channels + ch] = value;
    }
    if (!audio->write_output(samples, frames * channels, 10)) ++write_errors;
  }
  if (elapsed_ms - reported_ms < 1000) return;
  reported_ms = elapsed_ms;
  ESP_LOGI(kTag, "peak=%d samples=%lu read_errors=%lu write_errors=%lu", peak, static_cast<unsigned long>(received), static_cast<unsigned long>(read_errors), static_cast<unsigned long>(write_errors));
  peak = 0;
}

esp_err_t shutdown() {
  if (audio) {
    audio->deinitialize();
    if (audio->initialized()) return ESP_FAIL;
    delete audio; audio = nullptr;
  }
  return device_i2s_audio_release(&prepared.i2s_audio_claim);
}

const lunaros::esp_idf::DeviceTest<initialize, tick, shutdown> definition{kTag, 10};
}  // namespace

esp_err_t start_test(const TestConfig &config) { return definition.start(&config); }
esp_err_t stop_test() { return definition.stop(); }

}  // namespace lunaredu::device::i2s_audio
#else
namespace lunaredu::device::i2s_audio {
esp_err_t start_test(const TestConfig &) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t stop_test() { return ESP_OK; }
}  // namespace lunaredu::device::i2s_audio
#endif
