#include "lunaredu_audio_bridge.h"

#include <sdkconfig.h>
#include "pcm_backend.h"
#include "sample_ring.h"
#include <algorithm>
#include <memory>
#include <mutex>
#include <vector>

#include <esp_err.h>
#include <esp_log.h>



namespace lunaredu::audio {
namespace {

[[maybe_unused]] constexpr const char *kLogTag = "local_pcm_audio";

std::mutex g_mutex;
std::mutex g_output_mutex;
std::unique_ptr<PcmBackend> g_audio;
LocalPcmAudioBridgeDriver g_driver;
bool g_driver_configured = false;
LocalPcmAudioBridgeStatus g_status;
SampleRing g_output_reference;
std::vector<int16_t> g_scaled_output;


#ifndef CONFIG_LUNAREDU_AUDIO_REFERENCE_SAMPLES
#define CONFIG_LUNAREDU_AUDIO_REFERENCE_SAMPLES 8000
#endif
void append_output_reference_locked(const int16_t *samples, size_t sample_count) {
  if (samples == nullptr || sample_count == 0) return;
  for (size_t i = 0; i < sample_count; ++i) {
    if (g_output_reference.push(samples[i])) ++g_status.reference_drops;
  }
  ++g_status.reference_frames;
}

void set_error_locked(const std::string &error) {
  g_status.last_error = error;
  if (!error.empty()) {
    ESP_LOGW(kLogTag, "%s", error.c_str());
  }
}

} // namespace

bool configure_local_pcm_audio_bridge_driver(
    const LocalPcmAudioBridgeDriver &driver) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_audio != nullptr || g_status.initialized) {
    set_error_locked("local_audio_already_initialized");
    return false;
  }
  if (driver.input_sample_rate <= 0 || driver.output_sample_rate <= 0 ||
      driver.enable_input == nullptr || driver.enable_output == nullptr ||
      driver.configure_output == nullptr || driver.read_input == nullptr ||
      driver.write_output == nullptr) {
    set_error_locked("invalid_local_audio_driver");
    return false;
  }
  g_driver = driver;
  g_driver_configured = true;
  set_error_locked("");
  return true;
}

bool setup_local_pcm_audio_bridge() {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_audio != nullptr || (g_driver_configured && g_status.initialized)) {
    return true;
  }

  if (!g_output_reference.initialize(CONFIG_LUNAREDU_AUDIO_REFERENCE_SAMPLES)) {
    set_error_locked("audio_reference_allocation_failed");
    return false;
  }
  if (g_driver_configured) {
    g_status = {};
    g_status.initialized = true;
    g_status.input_sample_rate = g_driver.input_sample_rate;
    g_status.output_sample_rate = g_driver.output_sample_rate;
    g_output_reference.clear();
    set_error_locked("");
    ESP_LOGI(kLogTag, "custom local audio ready input=%d output=%d",
             g_status.input_sample_rate, g_status.output_sample_rate);
    return true;
  }

  std::string error;
  auto audio = create_pcm_backend(error);
  if (!audio) { set_error_locked(error); return false; }
  g_status.initialized = true;
  g_status.input_sample_rate = audio->input_sample_rate();
  g_status.output_sample_rate = audio->output_sample_rate();
  g_audio = std::move(audio);
  g_output_reference.clear();
  set_error_locked("");
  ESP_LOGI(kLogTag, "I2S local audio ready input=%d output=%d",
           g_status.input_sample_rate, g_status.output_sample_rate);
  return true;
}

LocalPcmAudioBridgeStatus local_pcm_audio_bridge_status() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_status;
}

bool local_pcm_audio_bridge_ready() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return (g_audio != nullptr || g_driver_configured) && g_status.initialized;
}

bool local_pcm_audio_bridge_enable_input(bool enable) {
  PcmBackend *audio = nullptr;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_audio == nullptr && !g_driver_configured) {
      set_error_locked("local_audio_not_initialized");
      return false;
    }
    audio = g_audio.get();
  }

  const bool ok = g_driver_configured
      ? g_driver.enable_input(g_driver.context, enable)
      : audio->enable_input(enable);

  std::lock_guard<std::mutex> lock(g_mutex);
  if (!ok) {
    set_error_locked(enable ? "enable_input_failed" : "disable_input_failed");
    return false;
  }
  g_status.input_enabled = enable;
  set_error_locked("");
  return true;
}

bool local_pcm_audio_bridge_enable_output(bool enable) {
  std::lock_guard<std::mutex> output_lock(g_output_mutex);
  PcmBackend *audio = nullptr;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_audio == nullptr && !g_driver_configured) {
      set_error_locked("local_audio_not_initialized");
      return false;
    }
    audio = g_audio.get();
  }

  const bool ok = g_driver_configured
      ? g_driver.enable_output(g_driver.context, enable)
      : audio->enable_output(enable);

  std::lock_guard<std::mutex> lock(g_mutex);
  if (!ok) {
    set_error_locked(enable ? "enable_output_failed" : "disable_output_failed");
    return false;
  }
  g_status.output_enabled = enable;
  if (!enable) g_output_reference.clear();
  set_error_locked("");
  return true;
}

bool local_pcm_audio_bridge_set_output_volume(uint8_t percent) {
  if (percent > 100) {
    return false;
  }
  std::lock_guard<std::mutex> lock(g_mutex);
  g_status.output_volume_percent = percent;
  return true;
}

bool local_pcm_audio_bridge_configure_output(uint32_t sample_rate) {
  std::lock_guard<std::mutex> output_lock(g_output_mutex);
  PcmBackend *audio = nullptr;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_audio == nullptr && !g_driver_configured) {
      set_error_locked("local_audio_not_initialized");
      return false;
    }
    audio = g_audio.get();
  }

  const bool ok = g_driver_configured
      ? g_driver.configure_output(g_driver.context, sample_rate)
      : audio->configure_output_sample_rate(sample_rate);

  std::lock_guard<std::mutex> lock(g_mutex);
  if (!ok) {
    set_error_locked("configure_output_failed");
    return false;
  }
  g_status.output_sample_rate =
      g_driver_configured ? static_cast<int>(sample_rate)
                          : audio->output_sample_rate();
  g_output_reference.clear();
  set_error_locked("");
  return true;
}

bool local_pcm_audio_bridge_read_input(int16_t *samples, size_t sample_count,
                                       uint32_t timeout_ms) {
  PcmBackend *audio = nullptr;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if ((g_audio == nullptr && !g_driver_configured) ||
        !g_status.input_enabled) {
      // Disabled input is an intentional resource state while another
      // foreground application owns audio focus, not a hardware read error.
      return false;
    }
    audio = g_audio.get();
  }

  const bool ok = g_driver_configured
      ? g_driver.read_input(g_driver.context, samples, sample_count, timeout_ms)
      : audio->read_input(samples, sample_count, timeout_ms);

  std::lock_guard<std::mutex> lock(g_mutex);
  if (!ok) {
    g_status.input_failures += 1;
    set_error_locked("local_audio_read_failed");
    return false;
  }
  g_status.input_frames += 1;
  set_error_locked("");
  return true;
}

bool local_pcm_audio_bridge_write_output(const int16_t *samples,
                                         size_t sample_count,
                                         uint32_t timeout_ms) {
  if (samples == nullptr || sample_count == 0) {
    return false;
  }
  std::lock_guard<std::mutex> output_lock(g_output_mutex);
  PcmBackend *audio = nullptr;
  uint8_t volume_percent = 100;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if ((g_audio == nullptr && !g_driver_configured) ||
        !g_status.output_enabled) {
      g_status.output_failures += 1;
      set_error_locked("local_audio_output_unavailable");
      return false;
    }
    audio = g_audio.get();
    volume_percent = g_status.output_volume_percent;
  }

  const int16_t *output_samples = samples;
  if (volume_percent < 100) {
    g_scaled_output.resize(sample_count);
    for (size_t index = 0; index < sample_count; ++index) {
      g_scaled_output[index] = static_cast<int16_t>(
          (static_cast<int32_t>(samples[index]) * volume_percent) / 100);
    }
    output_samples = g_scaled_output.data();
  }

  const bool ok = g_driver_configured
      ? g_driver.write_output(g_driver.context, output_samples, sample_count,
                              timeout_ms)
      : audio->write_output(output_samples, sample_count, timeout_ms);

  std::lock_guard<std::mutex> lock(g_mutex);
  if (!ok) {
    g_status.output_failures += 1;
    set_error_locked("local_audio_write_failed");
    return false;
  }
  append_output_reference_locked(output_samples, sample_count);
  g_status.output_frames += 1;
  set_error_locked("");
  return true;
}

bool local_pcm_audio_bridge_read_output_reference(int16_t *samples,
                                                  size_t sample_count) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (samples == nullptr || sample_count == 0) {
    return false;
  }
  const size_t available = g_output_reference.size();
  const size_t take = std::min(sample_count, available);
  const size_t pad = sample_count - take;
  for (size_t index = 0; index < take; ++index) {
    samples[index] = g_output_reference.pop();
  }
  if (pad > 0) {
    std::fill(samples + take, samples + sample_count, 0);
  }
  return true;
}

void local_pcm_audio_bridge_clear_output_reference() {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_output_reference.clear();
}

} // namespace lunaredu::audio
