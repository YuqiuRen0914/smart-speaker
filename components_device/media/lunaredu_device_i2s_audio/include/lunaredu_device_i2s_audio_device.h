#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

#include <esp_err.h>

#include "lunaredu_device_i2s_audio_config.h"

namespace lunaredu::device {

constexpr int kI2sPcmAudioGpioUnused = -1;
constexpr int kI2sPcmAudioPortAuto = -1;

enum class I2sPcmAudioSlotMask {
  kLeft,
  kRight,
  kBoth,
};

struct I2sPcmAudioPins {
  int mclk = kI2sPcmAudioGpioUnused;
  int bclk = kI2sPcmAudioGpioUnused;
  int ws = kI2sPcmAudioGpioUnused;
  int dout = kI2sPcmAudioGpioUnused;
  int din = kI2sPcmAudioGpioUnused;
};

struct I2sPcmAudioChannelConfig {
  bool enabled = false;
  int port = kI2sPcmAudioPortAuto;
  uint32_t sample_rate = 16000;
  int channels = 1;
  I2sPcmAudioPins pins;
  I2sPcmAudioSlotMask slot_mask = I2sPcmAudioSlotMask::kLeft;
  uint32_t dma_desc_num = 6;
  uint32_t dma_frame_num = 240;
};

struct I2sPcmAudioDeviceConfig {
  I2sPcmAudioChannelConfig input;
  I2sPcmAudioChannelConfig output;
  DeviceI2sAudioResourceClaim i2s_audio_claim{};
};

I2sPcmAudioDeviceConfig i2s_pcm_audio_device_config_from_i2s_audio(
    const DeviceI2sAudioConfig &audio_config);
I2sPcmAudioDeviceConfig i2s_pcm_audio_device_config_from_kconfig();
esp_err_t i2s_pcm_audio_device_prepare_from_i2s_audio(
    const DeviceI2sAudioConfig &audio_config,
    I2sPcmAudioDeviceConfig *device_config);
esp_err_t i2s_pcm_audio_device_prepare_from_kconfig(
    I2sPcmAudioDeviceConfig *device_config);

class I2sPcmAudioDevice final {
public:
  explicit I2sPcmAudioDevice(I2sPcmAudioDeviceConfig config);
  ~I2sPcmAudioDevice();

  I2sPcmAudioDevice(const I2sPcmAudioDevice &) = delete;
  I2sPcmAudioDevice &operator=(const I2sPcmAudioDevice &) = delete;

  bool initialize();
  void deinitialize();
  bool initialized() const;

  int input_sample_rate() const;
  int output_sample_rate() const;
  int input_channels() const;
  int output_channels() const;

  bool enable_input(bool enable);
  bool enable_output(bool enable);
  bool configure_output_sample_rate(uint32_t sample_rate);
  bool read_input(int16_t *samples, size_t sample_count,
                  uint32_t timeout_ms);
  bool write_output(const int16_t *samples, size_t sample_count,
                    uint32_t timeout_ms);

private:
  bool initialize_locked();
  void deinitialize_locked();
  bool initialize_channel(const I2sPcmAudioChannelConfig &channel,
                          bool transmit, void *&handle);
  bool initialize_duplex_channels();
  bool reconcile_channel_requests_locked();
  bool set_channel_enabled(void *handle, bool &current, bool enable);
  bool preload_output_silence_locked();
  bool write_output_silence_locked(size_t frame_count, uint32_t timeout_ms);

  I2sPcmAudioDeviceConfig config_;
  void *rx_handle_ = nullptr;
  void *tx_handle_ = nullptr;
  bool initialized_ = false;
  bool input_requested_ = false;
  bool output_requested_ = false;
  bool input_enabled_ = false;
  bool output_enabled_ = false;
  bool shared_duplex_clocks_ = false;
  bool output_uses_32bit_slots_ = false;
  std::vector<int32_t> rx_scratch_;
  std::vector<int16_t> tx_scratch_;
  std::vector<int32_t> tx_scratch_32_;
  mutable std::mutex mutex_;
};

} // namespace lunaredu::device
