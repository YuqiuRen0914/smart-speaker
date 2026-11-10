#include "lunaredu_device_i2s_audio_config.h"
#include "lunaredu_device_i2s_audio_runtime.h"

#include "bsp/lunaredu_bsp.h"

#include <array>
#include <cinttypes>
#include <cstddef>

#include <driver/i2s_common.h>
#include <esp_check.h>
#include <esp_log.h>

namespace lunaredu::device {
namespace {

constexpr const char *kLogTag = "device_i2s_audio";
bool is_valid_gpio(int pin) { return pin >= 0 && pin < GPIO_NUM_MAX; }
bool is_unused_gpio(int pin) { return pin < 0; }

template <size_t N> esp_err_t require_pins(const std::array<int, N> &pins) {
  for (int pin : pins) {
    if (!is_valid_gpio(pin)) {
      ESP_LOGE(kLogTag,
               "I2S audio GPIOs are not configured. Set the "
               "LUNAREDU_DEVICE_I2S_AUDIO_* Kconfig values for this board.");
      return ESP_ERR_INVALID_ARG;
    }
  }
  return ESP_OK;
}

template <size_t N> esp_err_t require_unique_pins(const std::array<int, N> &pins) {
  for (size_t i = 0; i < pins.size(); ++i) {
    if (is_unused_gpio(pins[i])) {
      continue;
    }
    for (size_t j = i + 1; j < pins.size(); ++j) {
      if (is_unused_gpio(pins[j])) {
        continue;
      }
      if (pins[i] == pins[j]) {
        ESP_LOGE(kLogTag, "I2S audio GPIO%d is assigned more than once",
                 pins[i]);
        return ESP_ERR_INVALID_ARG;
      }
    }
  }
  return ESP_OK;
}

template <size_t N> bool all_pins_valid(const std::array<int, N> &pins) {
  for (int pin : pins) {
    if (!is_valid_gpio(pin)) {
      return false;
    }
  }
  return true;
}

template <size_t N> bool all_pins_unused(const std::array<int, N> &pins) {
  for (int pin : pins) {
    if (!is_unused_gpio(pin)) {
      return false;
    }
  }
  return true;
}

std::array<int, 4> duplex_pins(const DeviceI2sDuplexAudioPins &pins) {
  return {pins.bclk, pins.ws, pins.dout, pins.din};
}

std::array<int, 3> simplex_speaker_pins(
    const DeviceI2sSimplexAudioPins &pins) {
  return {pins.speaker_bclk, pins.speaker_ws, pins.speaker_dout};
}

std::array<int, 3> simplex_microphone_pins(
    const DeviceI2sSimplexAudioPins &pins) {
  return {pins.microphone_sck, pins.microphone_ws, pins.microphone_din};
}

std::array<int, 6> active_pins(const DeviceI2sAudioConfig &config) {
  if (config.topology == DeviceI2sAudioTopology::kDuplex) {
    const auto pins = duplex_pins(config.duplex);
    return {pins[0], pins[1], pins[2], pins[3], -1, -1};
  }

  const auto speaker = simplex_speaker_pins(config.simplex);
  const auto microphone = simplex_microphone_pins(config.simplex);
  return {all_pins_valid(speaker) ? speaker[0] : -1,
          all_pins_valid(speaker) ? speaker[1] : -1,
          all_pins_valid(speaker) ? speaker[2] : -1,
          all_pins_valid(microphone) ? microphone[0] : -1,
          all_pins_valid(microphone) ? microphone[1] : -1,
          all_pins_valid(microphone) ? microphone[2] : -1};
}

} // namespace

DeviceI2sAudioConfig device_i2s_audio_config_from_kconfig() {
  DeviceI2sAudioConfig config;
#if CONFIG_LUNAREDU_DEVICE_I2S_AUDIO_DUPLEX
  config.topology = DeviceI2sAudioTopology::kDuplex;
#else
  config.topology = DeviceI2sAudioTopology::kSimplex;
#endif
#if CONFIG_LUNAREDU_DEVICE_I2S_AUDIO_MIC_SLOT_RIGHT
  config.mic_slot = DeviceI2sAudioMicSlot::kRight;
#elif CONFIG_LUNAREDU_DEVICE_I2S_AUDIO_MIC_SLOT_BOTH
  config.mic_slot = DeviceI2sAudioMicSlot::kBoth;
#else
  config.mic_slot = DeviceI2sAudioMicSlot::kLeft;
#endif
  config.input_sample_rate = CONFIG_LUNAREDU_DEVICE_I2S_AUDIO_INPUT_SAMPLE_RATE;
  config.output_sample_rate =
      CONFIG_LUNAREDU_DEVICE_I2S_AUDIO_OUTPUT_SAMPLE_RATE;
  config.duplex.bclk = CONFIG_LUNAREDU_DEVICE_I2S_AUDIO_BCLK;
  config.duplex.ws = CONFIG_LUNAREDU_DEVICE_I2S_AUDIO_WS;
  config.duplex.dout = CONFIG_LUNAREDU_DEVICE_I2S_AUDIO_DOUT;
  config.duplex.din = CONFIG_LUNAREDU_DEVICE_I2S_AUDIO_DIN;
  config.simplex.speaker_bclk = CONFIG_LUNAREDU_DEVICE_I2S_AUDIO_SPK_BCLK;
  config.simplex.speaker_ws = CONFIG_LUNAREDU_DEVICE_I2S_AUDIO_SPK_WS;
  config.simplex.speaker_dout = CONFIG_LUNAREDU_DEVICE_I2S_AUDIO_SPK_DOUT;
  config.simplex.microphone_sck = CONFIG_LUNAREDU_DEVICE_I2S_AUDIO_MIC_SCK;
  config.simplex.microphone_ws = CONFIG_LUNAREDU_DEVICE_I2S_AUDIO_MIC_WS;
  config.simplex.microphone_din = CONFIG_LUNAREDU_DEVICE_I2S_AUDIO_MIC_DIN;
  return config;
}

const char *device_i2s_audio_mic_slot_name(DeviceI2sAudioMicSlot slot) {
  switch (slot) {
  case DeviceI2sAudioMicSlot::kLeft:
    return "left";
  case DeviceI2sAudioMicSlot::kRight:
    return "right";
  case DeviceI2sAudioMicSlot::kBoth:
    return "both";
  default:
    return "unknown";
  }
}

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

void device_i2s_audio_log_config(const DeviceI2sAudioConfig &config) {
  ESP_LOGI(kLogTag,
           "topology=%s input_rate=%" PRIu32 " output_rate=%" PRIu32 " mic_slot=%s",
           device_i2s_audio_topology_name(config.topology),
           config.input_sample_rate, config.output_sample_rate,
           device_i2s_audio_mic_slot_name(config.mic_slot));

  if (config.topology == DeviceI2sAudioTopology::kDuplex) {
    ESP_LOGI(kLogTag, "duplex pins bclk=%d ws=%d dout=%d din=%d",
             config.duplex.bclk, config.duplex.ws, config.duplex.dout,
             config.duplex.din);
    return;
  }

  ESP_LOGI(kLogTag,
           "simplex pins spk_bclk=%d spk_ws=%d spk_dout=%d mic_sck=%d "
           "mic_ws=%d mic_din=%d",
           config.simplex.speaker_bclk, config.simplex.speaker_ws,
           config.simplex.speaker_dout, config.simplex.microphone_sck,
           config.simplex.microphone_ws, config.simplex.microphone_din);
}

esp_err_t device_i2s_audio_prepare(const DeviceI2sAudioConfig &config) {
  return device_i2s_audio_prepare(config, nullptr);
}

esp_err_t device_i2s_audio_prepare(const DeviceI2sAudioConfig &config,
                                   DeviceI2sAudioResourceClaim *claim) {
  if (claim != nullptr) {
    *claim = DeviceI2sAudioResourceClaim();
  }

  device_i2s_audio_log_config(config);

  if (config.topology == DeviceI2sAudioTopology::kDuplex) {
    if (config.input_sample_rate != config.output_sample_rate) {
      ESP_LOGE(kLogTag,
               "duplex audio requires matching input/output sample rates "
               "because BCLK/WS are shared: input=%" PRIu32 " output=%" PRIu32,
               config.input_sample_rate, config.output_sample_rate);
      return ESP_ERR_INVALID_ARG;
    }
    const auto pins = duplex_pins(config.duplex);
    ESP_RETURN_ON_ERROR(require_pins(pins), kLogTag,
                        "duplex audio pins are not configured");
    ESP_RETURN_ON_ERROR(require_unique_pins(pins), kLogTag,
                        "duplex audio pins are not unique");
  } else {
    const auto speaker = simplex_speaker_pins(config.simplex);
    const auto microphone = simplex_microphone_pins(config.simplex);
    const bool speaker_enabled = all_pins_valid(speaker);
    const bool microphone_enabled = all_pins_valid(microphone);
    if (!speaker_enabled && !all_pins_unused(speaker)) {
      ESP_RETURN_ON_ERROR(require_pins(speaker), kLogTag,
                          "simplex speaker pins are not configured");
    }
    if (!microphone_enabled && !all_pins_unused(microphone)) {
      ESP_RETURN_ON_ERROR(require_pins(microphone), kLogTag,
                          "simplex microphone pins are not configured");
    }
    if (!speaker_enabled && !microphone_enabled) {
      ESP_LOGE(kLogTag, "simplex audio has no speaker or microphone pins");
      return ESP_ERR_INVALID_ARG;
    }
    const auto pins = active_pins(config);
    ESP_RETURN_ON_ERROR(require_unique_pins(pins), kLogTag,
                        "simplex audio pins are not unique");
  }

  const auto pins = active_pins(config);
  const esp_err_t result =
      bsp_i2s_claim_pins(pins.data(), pins.size(), claim);
  if (result != ESP_OK) {
    const bsp_resource_claim_snapshot_t last_claim =
        bsp_resource_last_claim_snapshot();
    ESP_LOGE(kLogTag, "failed to reserve BSP I2S pin groups: %s owner=%s pin=%s",
             esp_err_to_name(result),
             last_claim.conflict_owner[0] != '\0' ? last_claim.conflict_owner
                                                  : "(none)",
             last_claim.conflict_pin[0] != '\0' ? last_claim.conflict_pin
                                                : "(none)");
  }
  return result;
}

esp_err_t device_i2s_audio_release(DeviceI2sAudioResourceClaim *claim) {
  return bsp_i2s_release_pins(claim);
}

esp_err_t device_i2s_audio_delete_adopted_channel(void *channel_handle,
                                                  bool enabled) {
  if (channel_handle == nullptr) {
    return ESP_OK;
  }

  esp_err_t first_error = ESP_OK;
  const auto handle = static_cast<i2s_chan_handle_t>(channel_handle);
  if (enabled) {
    const esp_err_t disable_result = i2s_channel_disable(handle);
    if (disable_result != ESP_OK &&
        disable_result != ESP_ERR_INVALID_STATE) {
      first_error = disable_result;
    }
  }

  const esp_err_t delete_result = i2s_del_channel(handle);
  if (first_error == ESP_OK && delete_result != ESP_OK) {
    first_error = delete_result;
  }
  return first_error;
}

esp_err_t device_i2s_audio_release_runtime(
    DeviceI2sAudioResourceClaim *claim, DeviceI2sAudioAdoptedChannels *channels) {
  esp_err_t first_error = ESP_OK;
  auto record = [&first_error](esp_err_t result) {
    if (first_error == ESP_OK && result != ESP_OK &&
        result != ESP_ERR_NOT_SUPPORTED) {
      first_error = result;
    }
  };

  if (channels != nullptr) {
    record(device_i2s_audio_delete_adopted_channel(channels->rx_handle,
                                                   channels->rx_enabled));
    channels->rx_handle = nullptr;
    channels->rx_enabled = false;

    record(device_i2s_audio_delete_adopted_channel(channels->tx_handle,
                                                   channels->tx_enabled));
    channels->tx_handle = nullptr;
    channels->tx_enabled = false;
  }

  if (claim != nullptr) {
    record(device_i2s_audio_release(claim));
  }

  return first_error;
}

bool device_i2s_audio_uses_shared_bsp_pins(
    const DeviceI2sAudioConfig &config) {
  const auto pins = active_pins(config);
  return bsp_i2s_uses_shared_pins(pins.data(), pins.size());
}

} // namespace lunaredu::device
