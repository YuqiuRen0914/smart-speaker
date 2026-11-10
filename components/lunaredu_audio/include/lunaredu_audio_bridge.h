#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace lunaredu::audio {

struct LocalPcmAudioBridgeStatus {
  bool initialized = false;
  bool input_enabled = false;
  bool output_enabled = false;
  uint8_t output_volume_percent = 70;
  int input_sample_rate = 0;
  int output_sample_rate = 0;
  uint32_t input_frames = 0;
  uint32_t output_frames = 0;
  uint32_t input_failures = 0;
  uint32_t output_failures = 0;
  uint32_t reference_frames = 0;
  uint32_t reference_drops = 0;
  std::string last_error;
};

struct LocalPcmAudioBridgeDriver {
  void *context = nullptr;
  int input_sample_rate = 0;
  int output_sample_rate = 0;
  bool (*enable_input)(void *context, bool enable) = nullptr;
  bool (*enable_output)(void *context, bool enable) = nullptr;
  bool (*configure_output)(void *context, uint32_t sample_rate) = nullptr;
  bool (*read_input)(void *context, int16_t *samples, size_t sample_count,
                     uint32_t timeout_ms) = nullptr;
  bool (*write_output)(void *context, const int16_t *samples,
                       size_t sample_count, uint32_t timeout_ms) = nullptr;
};

// Optional product-owned audio backend. Must be configured before
// setup_local_pcm_audio_bridge(); ignored after the bridge is initialized.
bool configure_local_pcm_audio_bridge_driver(
    const LocalPcmAudioBridgeDriver &driver);
bool setup_local_pcm_audio_bridge();
LocalPcmAudioBridgeStatus local_pcm_audio_bridge_status();
bool local_pcm_audio_bridge_ready();
bool local_pcm_audio_bridge_enable_input(bool enable);
bool local_pcm_audio_bridge_enable_output(bool enable);
bool local_pcm_audio_bridge_set_output_volume(uint8_t percent);
bool local_pcm_audio_bridge_configure_output(uint32_t sample_rate);
bool local_pcm_audio_bridge_read_input(int16_t *samples, size_t sample_count,
                                       uint32_t timeout_ms);
bool local_pcm_audio_bridge_write_output(const int16_t *samples,
                                         size_t sample_count,
                                         uint32_t timeout_ms);
bool local_pcm_audio_bridge_read_output_reference(int16_t *samples,
                                                  size_t sample_count);
void local_pcm_audio_bridge_clear_output_reference();

} // namespace lunaredu::audio
