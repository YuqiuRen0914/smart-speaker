#include "sdkconfig.h"
#include <string>
#include <utility>
#include "lunaredu_device_i2s_audio_config.h"
#include "lunaredu_device_i2s_audio_device.h"
#include "lunaredu_device_i2s_audio_runtime.h"

namespace {
[[maybe_unused]] constexpr const char *kDisabledError = "disabled";
[[maybe_unused]] void set_error(std::string &error, const char *message = kDisabledError) {
  error = message == nullptr ? kDisabledError : message;
}
}

#if !CONFIG_LUNAREDU_DEVICE_I2S_AUDIO

namespace lunaredu::device {

DeviceI2sAudioConfig device_i2s_audio_config_from_kconfig() { return {}; }

const char *device_i2s_audio_topology_name(DeviceI2sAudioTopology topology) {
  switch (topology) {
  case DeviceI2sAudioTopology::kDuplex:
    return "duplex";
  case DeviceI2sAudioTopology::kSimplex:
    return "simplex";
  default:
    return "unknown";
  }
}

void device_i2s_audio_log_config(const DeviceI2sAudioConfig &) {}

esp_err_t device_i2s_audio_prepare(const DeviceI2sAudioConfig &) {
  return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t device_i2s_audio_prepare(const DeviceI2sAudioConfig &,
                                   DeviceI2sAudioResourceClaim *claim) {
  if (claim != nullptr) {
    *claim = DeviceI2sAudioResourceClaim();
  }
  return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t device_i2s_audio_release(DeviceI2sAudioResourceClaim *claim) {
  if (claim != nullptr) {
    *claim = DeviceI2sAudioResourceClaim();
  }
  return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t device_i2s_audio_delete_adopted_channel(void *channel_handle,
                                                  bool) {
  return channel_handle == nullptr ? ESP_OK : ESP_ERR_NOT_SUPPORTED;
}

esp_err_t device_i2s_audio_release_runtime(
    DeviceI2sAudioResourceClaim *claim, DeviceI2sAudioAdoptedChannels *channels) {
  if (channels != nullptr) {
    *channels = DeviceI2sAudioAdoptedChannels();
  }
  if (claim != nullptr) {
    *claim = DeviceI2sAudioResourceClaim();
  }
  return ESP_ERR_NOT_SUPPORTED;
}

bool device_i2s_audio_uses_shared_bsp_pins(
    const DeviceI2sAudioConfig &) {
  return false;
}

} // namespace lunaredu::device

#endif

