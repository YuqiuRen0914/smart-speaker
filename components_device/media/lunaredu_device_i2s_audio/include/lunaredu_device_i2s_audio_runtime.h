#pragma once

#include <esp_err.h>

#include "lunaredu_device_i2s_audio_config.h"

namespace lunaredu::device {

struct DeviceI2sAudioAdoptedChannels {
  void *rx_handle = nullptr;
  bool rx_enabled = false;
  void *tx_handle = nullptr;
  bool tx_enabled = false;
};

esp_err_t device_i2s_audio_delete_adopted_channel(void *channel_handle,
                                                  bool enabled);
esp_err_t device_i2s_audio_release_runtime(
    DeviceI2sAudioResourceClaim *claim, DeviceI2sAudioAdoptedChannels *channels);

} // namespace lunaredu::device
