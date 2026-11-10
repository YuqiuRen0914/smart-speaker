#pragma once

#include <cstdint>

#include <esp_err.h>

#include "bsp/lunaredu_bsp.h"

namespace lunaredu::device {

enum class DeviceI2sAudioTopology {
  kDuplex,
  kSimplex,
};

enum class DeviceI2sAudioMicSlot {
  kLeft,
  kRight,
  kBoth,
};

struct DeviceI2sDuplexAudioPins {
  int bclk = -1;
  int ws = -1;
  int dout = -1;
  int din = -1;
};

struct DeviceI2sSimplexAudioPins {
  int speaker_bclk = -1;
  int speaker_ws = -1;
  int speaker_dout = -1;
  int microphone_sck = -1;
  int microphone_ws = -1;
  int microphone_din = -1;
};

struct DeviceI2sAudioConfig {
  DeviceI2sAudioTopology topology = DeviceI2sAudioTopology::kDuplex;
  uint32_t input_sample_rate = 24000;
  uint32_t output_sample_rate = 24000;
  DeviceI2sAudioMicSlot mic_slot = DeviceI2sAudioMicSlot::kLeft;
  DeviceI2sDuplexAudioPins duplex;
  DeviceI2sSimplexAudioPins simplex;
};

using DeviceI2sAudioResourceClaim = bsp_i2s_resource_claim_t;

DeviceI2sAudioConfig device_i2s_audio_config_from_kconfig();
const char *device_i2s_audio_topology_name(DeviceI2sAudioTopology topology);
void device_i2s_audio_log_config(const DeviceI2sAudioConfig &config);
esp_err_t device_i2s_audio_prepare(const DeviceI2sAudioConfig &config);
esp_err_t device_i2s_audio_prepare(const DeviceI2sAudioConfig &config,
                                   DeviceI2sAudioResourceClaim *claim);
esp_err_t device_i2s_audio_release(DeviceI2sAudioResourceClaim *claim);
bool device_i2s_audio_uses_shared_bsp_pins(const DeviceI2sAudioConfig &config);

} // namespace lunaredu::device
