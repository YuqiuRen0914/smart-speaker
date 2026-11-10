#include "lunaredu_device_i2s_audio_device.h"

#include <algorithm>
#include <cinttypes>
#include <driver/i2s_common.h>
#include <driver/i2s_std.h>
#include <esp_err.h>
#include <esp_log.h>
#include <limits>
#include <vector>

namespace lunaredu::device {
namespace {

constexpr const char *kLogTag = "i2s_pcm_audio";
constexpr size_t kOutputSilenceMinFrames = 96;
constexpr uint32_t kOutputSilenceTimeoutMs = 80;
// The direct INMP441 path has no codec-side input-gain register.  Keeping only
// the top 16 bits of its 32-bit I2S slot leaves ordinary speech at a very low
// PCM level (while tap-to-talk can still upload it).  A small digital gain
// brings the signal into WakeNet's useful range; saturation protection keeps
// loud speech from wrapping.  This is deliberately conservative because the
// AFE still performs its own noise suppression and VAD.
constexpr int32_t kMicrophoneDigitalGain = 4;

gpio_num_t to_gpio(int pin) { return static_cast<gpio_num_t>(pin); }
bool valid_gpio(int pin) { return pin >= 0 && pin < GPIO_NUM_MAX; }

i2s_slot_mode_t slot_mode_for_channels(int channels) {
  return channels > 1 ? I2S_SLOT_MODE_STEREO : I2S_SLOT_MODE_MONO;
}

i2s_std_slot_mask_t to_slot_mask(I2sPcmAudioSlotMask mask) {
  switch (mask) {
  case I2sPcmAudioSlotMask::kLeft:
    return I2S_STD_SLOT_LEFT;
  case I2sPcmAudioSlotMask::kRight:
    return I2S_STD_SLOT_RIGHT;
  case I2sPcmAudioSlotMask::kBoth:
    return I2S_STD_SLOT_BOTH;
  default:
    return I2S_STD_SLOT_LEFT;
  }
}

I2sPcmAudioSlotMask microphone_slot_mask(DeviceI2sAudioMicSlot slot) {
  switch (slot) {
  case DeviceI2sAudioMicSlot::kRight:
    return I2sPcmAudioSlotMask::kRight;
  case DeviceI2sAudioMicSlot::kBoth:
    return I2sPcmAudioSlotMask::kBoth;
  case DeviceI2sAudioMicSlot::kLeft:
  default:
    return I2sPcmAudioSlotMask::kLeft;
  }
}

I2sPcmAudioChannelConfig mono_channel(uint32_t sample_rate) {
  I2sPcmAudioChannelConfig channel;
  channel.enabled = true;
  channel.sample_rate = sample_rate;
  channel.channels = 1;
  return channel;
}

I2sPcmAudioChannelConfig stereo_output_channel(uint32_t sample_rate) {
  I2sPcmAudioChannelConfig channel;
  channel.enabled = true;
  channel.sample_rate = sample_rate;
  channel.channels = 2;
  channel.slot_mask = I2sPcmAudioSlotMask::kBoth;
  return channel;
}

int16_t clamp_i2s_32bit_input(int32_t value) {
  // INMP441 outputs 24-bit two's-complement samples in a 32-bit I2S slot.
  // Keep the most significant 16 bits, then apply the small board-level gain
  // above so normal speech reaches WakeNet without wrapping or clipping.
  const int64_t amplified = static_cast<int64_t>(value >> 16) *
                             kMicrophoneDigitalGain;
  const int32_t pcm16 = amplified > std::numeric_limits<int32_t>::max()
                            ? std::numeric_limits<int32_t>::max()
                            : amplified < std::numeric_limits<int32_t>::min()
                                  ? std::numeric_limits<int32_t>::min()
                                  : static_cast<int32_t>(amplified);
  if (pcm16 > std::numeric_limits<int16_t>::max()) {
    return std::numeric_limits<int16_t>::max();
  }
  if (pcm16 < std::numeric_limits<int16_t>::min()) {
    return std::numeric_limits<int16_t>::min();
  }
  return static_cast<int16_t>(pcm16);
}

} // namespace

I2sPcmAudioDeviceConfig i2s_pcm_audio_device_config_from_i2s_audio(
    const DeviceI2sAudioConfig &audio_config) {
  I2sPcmAudioDeviceConfig config;

  if (audio_config.topology == DeviceI2sAudioTopology::kDuplex) {
    config.input = mono_channel(audio_config.input_sample_rate);
    config.output = stereo_output_channel(audio_config.output_sample_rate);
    config.input.channels = 2;
    config.input.slot_mask = microphone_slot_mask(audio_config.mic_slot);
    config.input.port = 0;
    config.output.port = 0;
    config.input.pins.bclk = audio_config.duplex.bclk;
    config.input.pins.ws = audio_config.duplex.ws;
    config.input.pins.din = audio_config.duplex.din;
    config.output.pins.bclk = audio_config.duplex.bclk;
    config.output.pins.ws = audio_config.duplex.ws;
    config.output.pins.dout = audio_config.duplex.dout;
    return config;
  }

  const bool input_enabled =
      valid_gpio(audio_config.simplex.microphone_sck) &&
      valid_gpio(audio_config.simplex.microphone_ws) &&
      valid_gpio(audio_config.simplex.microphone_din);
  const bool output_enabled =
      valid_gpio(audio_config.simplex.speaker_bclk) &&
      valid_gpio(audio_config.simplex.speaker_ws) &&
      valid_gpio(audio_config.simplex.speaker_dout);

  if (input_enabled) {
    config.input = mono_channel(audio_config.input_sample_rate);
    config.input.slot_mask = microphone_slot_mask(audio_config.mic_slot);
    if (audio_config.mic_slot == DeviceI2sAudioMicSlot::kBoth) {
      config.input.channels = 2;
    }
    config.input.port = 1;
    config.input.pins.bclk = audio_config.simplex.microphone_sck;
    config.input.pins.ws = audio_config.simplex.microphone_ws;
    config.input.pins.din = audio_config.simplex.microphone_din;
  }
  if (output_enabled) {
    config.output = stereo_output_channel(audio_config.output_sample_rate);
    config.output.port = 0;
    config.output.pins.bclk = audio_config.simplex.speaker_bclk;
    config.output.pins.ws = audio_config.simplex.speaker_ws;
    config.output.pins.dout = audio_config.simplex.speaker_dout;
  }
  return config;
}

I2sPcmAudioDeviceConfig i2s_pcm_audio_device_config_from_kconfig() {
  return i2s_pcm_audio_device_config_from_i2s_audio(
      device_i2s_audio_config_from_kconfig());
}

esp_err_t i2s_pcm_audio_device_prepare_from_i2s_audio(
    const DeviceI2sAudioConfig &audio_config,
    I2sPcmAudioDeviceConfig *device_config) {
  if (device_config == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }

  DeviceI2sAudioResourceClaim claim{};
  const esp_err_t result = device_i2s_audio_prepare(audio_config, &claim);
  if (result != ESP_OK) {
    *device_config = I2sPcmAudioDeviceConfig();
    return result;
  }

  *device_config = i2s_pcm_audio_device_config_from_i2s_audio(audio_config);
  device_config->i2s_audio_claim = claim;
  return ESP_OK;
}

esp_err_t i2s_pcm_audio_device_prepare_from_kconfig(
    I2sPcmAudioDeviceConfig *device_config) {
  return i2s_pcm_audio_device_prepare_from_i2s_audio(
      device_i2s_audio_config_from_kconfig(), device_config);
}

I2sPcmAudioDevice::I2sPcmAudioDevice(I2sPcmAudioDeviceConfig config)
    : config_(config) {}

I2sPcmAudioDevice::~I2sPcmAudioDevice() { deinitialize(); }

bool I2sPcmAudioDevice::initialize() {
  std::lock_guard<std::mutex> lock(mutex_);
  return initialize_locked();
}

void I2sPcmAudioDevice::deinitialize() {
  std::lock_guard<std::mutex> lock(mutex_);
  deinitialize_locked();
}

bool I2sPcmAudioDevice::initialized() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return initialized_;
}

int I2sPcmAudioDevice::input_sample_rate() const {
  return config_.input.sample_rate;
}

int I2sPcmAudioDevice::output_sample_rate() const {
  return config_.output.sample_rate;
}

int I2sPcmAudioDevice::input_channels() const { return config_.input.channels; }

int I2sPcmAudioDevice::output_channels() const {
  return config_.output.channels;
}

bool I2sPcmAudioDevice::enable_input(bool enable) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (enable && !initialize_locked()) {
    return false;
  }
  const bool previous = input_requested_;
  input_requested_ = enable;
  if (!reconcile_channel_requests_locked()) {
    input_requested_ = previous;
    (void)reconcile_channel_requests_locked();
    return false;
  }
  return true;
}

bool I2sPcmAudioDevice::enable_output(bool enable) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (enable && !initialize_locked()) {
    return false;
  }
  const bool previous = output_requested_;
  output_requested_ = enable;
  if (!reconcile_channel_requests_locked()) {
    output_requested_ = previous;
    (void)reconcile_channel_requests_locked();
    return false;
  }
  return true;
}

bool I2sPcmAudioDevice::configure_output_sample_rate(uint32_t sample_rate) {
  if (sample_rate == 0) {
    return false;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  if (!config_.output.enabled) {
    ESP_LOGE(kLogTag, "output channel is not enabled");
    return false;
  }
  if (shared_duplex_clocks_ && config_.input.enabled &&
      sample_rate != config_.input.sample_rate) {
    ESP_LOGE(kLogTag,
             "duplex output rate must match input rate: input=%" PRIu32 " output=%" PRIu32,
             config_.input.sample_rate, sample_rate);
    return false;
  }
  if (config_.output.sample_rate == sample_rate) {
    return true;
  }

  const uint32_t previous_rate = config_.output.sample_rate;
  config_.output.sample_rate = sample_rate;
  if (!initialized_ || tx_handle_ == nullptr) {
    return true;
  }

  const bool was_enabled = output_enabled_;
  if (was_enabled) {
    (void)write_output_silence_locked(config_.output.dma_frame_num,
                                      kOutputSilenceTimeoutMs);
  }
  if (was_enabled && !set_channel_enabled(tx_handle_, output_enabled_, false)) {
    config_.output.sample_rate = previous_rate;
    return false;
  }

  i2s_std_clk_config_t clock = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate);
  const esp_err_t result = i2s_channel_reconfig_std_clock(
      static_cast<i2s_chan_handle_t>(tx_handle_), &clock);
  if (result != ESP_OK) {
    ESP_LOGE(kLogTag, "failed to reconfigure output clock rate=%" PRIu32 ": %s",
             sample_rate, esp_err_to_name(result));
    config_.output.sample_rate = previous_rate;
    if (was_enabled) {
      (void)set_channel_enabled(tx_handle_, output_enabled_, true);
    }
    return false;
  }

  if (was_enabled && !set_channel_enabled(tx_handle_, output_enabled_, true)) {
    return false;
  }
  ESP_LOGI(kLogTag, "output clock reconfigured rate=%" PRIu32,
           sample_rate);
  return true;
}

bool I2sPcmAudioDevice::read_input(int16_t *samples, size_t sample_count,
                                   uint32_t timeout_ms) {
  if (samples == nullptr || sample_count == 0) {
    return false;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  if (!input_requested_ || !input_enabled_ || rx_handle_ == nullptr) {
    return false;
  }

  const size_t input_channels =
      config_.input.channels > 1 ? static_cast<size_t>(config_.input.channels)
                                  : 1U;
  const size_t raw_sample_count = sample_count * input_channels;
  rx_scratch_.resize(raw_sample_count);
  size_t bytes_read = 0;
  const size_t byte_count = raw_sample_count * sizeof(int32_t);
  const esp_err_t result =
      i2s_channel_read(static_cast<i2s_chan_handle_t>(rx_handle_),
                       rx_scratch_.data(), byte_count, &bytes_read,
                       timeout_ms);
  if (bytes_read == 0 || bytes_read % sizeof(int32_t) != 0) {
    static uint32_t s_read_failures = 0;
    s_read_failures += 1;
    if (s_read_failures <= 8 || (s_read_failures % 100) == 0) {
      ESP_LOGW(kLogTag,
               "read_input failed count=%" PRIu32 " result=%s bytes=%u requested=%u timeout_ms=%" PRIu32,
               s_read_failures, esp_err_to_name(result),
               static_cast<unsigned>(bytes_read),
               static_cast<unsigned>(byte_count), timeout_ms);
    }
    return false;
  }
  if (result != ESP_OK) {
    static uint32_t s_partial_reads = 0;
    s_partial_reads += 1;
    if (s_partial_reads <= 8 || (s_partial_reads % 100) == 0) {
      ESP_LOGW(kLogTag,
               "read_input partial count=%" PRIu32 " result=%s bytes=%u requested=%u timeout_ms=%" PRIu32,
               s_partial_reads, esp_err_to_name(result),
               static_cast<unsigned>(bytes_read),
               static_cast<unsigned>(byte_count), timeout_ms);
    }
  }

  const size_t raw_samples_read =
      std::min(raw_sample_count, bytes_read / sizeof(int32_t));
  const size_t frames_read = raw_samples_read / input_channels;
#if CONFIG_LOG_MAXIMUM_LEVEL >= 4
  int32_t raw_min = std::numeric_limits<int32_t>::max();
  int32_t raw_max = std::numeric_limits<int32_t>::min();
  int16_t pcm_min = std::numeric_limits<int16_t>::max();
  int16_t pcm_max = std::numeric_limits<int16_t>::min();
  int64_t raw_sum = 0;
  int64_t pcm_sum = 0;
  uint32_t saturated_samples = 0;
#endif
  // A duplex I2S bus is read as two slots so its clocks remain compatible
  // with the stereo speaker path.  The microphone itself still occupies one
  // configured slot.  Never select a slot from per-frame energy: adjacent I2S
  // slots can contain nearly identical samples, and switching between them
  // frame-by-frame breaks the continuous waveform required by WakeNet.
  const size_t selected_slot =
      input_channels == 2 && config_.input.slot_mask == I2sPcmAudioSlotMask::kRight
          ? 1U
          : 0U;
  for (size_t frame = 0; frame < frames_read; ++frame) {
    const int32_t raw = rx_scratch_[frame * input_channels + selected_slot];
    const int16_t pcm = clamp_i2s_32bit_input(raw);
    samples[frame] = pcm;
#if CONFIG_LOG_MAXIMUM_LEVEL >= 4
    raw_min = std::min(raw_min, raw);
    raw_max = std::max(raw_max, raw);
    pcm_min = std::min(pcm_min, pcm);
    pcm_max = std::max(pcm_max, pcm);
    raw_sum += raw;
    pcm_sum += pcm;
    if (pcm == std::numeric_limits<int16_t>::max() ||
        pcm == std::numeric_limits<int16_t>::min()) {
      saturated_samples += 1;
    }
#endif
  }
#if CONFIG_LOG_MAXIMUM_LEVEL >= 4
  static uint32_t s_conversion_frames = 0;
  s_conversion_frames += 1;
  if (frames_read > 0 &&
      (s_conversion_frames <= 8 || (s_conversion_frames % 200) == 0)) {
    ESP_LOGD(kLogTag,
             "input conversion frames=%" PRIu32 " samples=%u raw32_min=%" PRId32 " raw32_max=%" PRId32 " raw32_mean=%" PRId64 " pcm16_min=%" PRId16 " pcm16_max=%" PRId16 " pcm16_mean=%" PRId64 " saturated=%" PRIu32,
             s_conversion_frames, static_cast<unsigned>(frames_read),
             raw_min, raw_max, raw_sum / static_cast<int64_t>(frames_read),
             pcm_min, pcm_max, pcm_sum / static_cast<int64_t>(frames_read),
             saturated_samples);
  }
#endif
  if (frames_read < sample_count) {
    std::fill(samples + frames_read, samples + sample_count, 0);
  }
  return true;
}

bool I2sPcmAudioDevice::write_output(const int16_t *samples,
                                     size_t sample_count,
                                     uint32_t timeout_ms) {
  if (samples == nullptr || sample_count == 0) {
    return false;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  if (!output_requested_ || !output_enabled_ || tx_handle_ == nullptr) {
    return false;
  }

  const int output_channels = std::max(1, config_.output.channels);
  const size_t slot_count =
      sample_count * static_cast<size_t>(output_channels);
  if (output_uses_32bit_slots_) {
    tx_scratch_32_.resize(slot_count);
    for (size_t frame = 0; frame < sample_count; ++frame) {
      const int32_t sample32 = static_cast<int32_t>(samples[frame]) << 16;
      for (int channel = 0; channel < output_channels; ++channel) {
        tx_scratch_32_[frame * static_cast<size_t>(output_channels) +
                       static_cast<size_t>(channel)] = sample32;
      }
    }

    size_t bytes_written = 0;
    const size_t byte_count = tx_scratch_32_.size() * sizeof(int32_t);
    const esp_err_t result =
        i2s_channel_write(static_cast<i2s_chan_handle_t>(tx_handle_),
                          tx_scratch_32_.data(), byte_count, &bytes_written,
                          timeout_ms);
    const bool ok = result == ESP_OK && bytes_written == byte_count;
    if (!ok) {
      static uint32_t s_write_failures = 0;
      s_write_failures += 1;
      if (s_write_failures <= 8 || (s_write_failures % 100) == 0) {
        ESP_LOGW(kLogTag,
                 "write_output failed count=%" PRIu32 " result=%s bytes=%u requested=%u samples=%u timeout_ms=%" PRIu32,
                 s_write_failures, esp_err_to_name(result),
                 static_cast<unsigned>(bytes_written),
                 static_cast<unsigned>(byte_count),
                 static_cast<unsigned>(sample_count), timeout_ms);
      }
    }
    return ok;
  }

  tx_scratch_.resize(slot_count);
  for (size_t frame = 0; frame < sample_count; ++frame) {
    for (int channel = 0; channel < output_channels; ++channel) {
      tx_scratch_[frame * static_cast<size_t>(output_channels) +
                  static_cast<size_t>(channel)] = samples[frame];
    }
  }

  size_t bytes_written = 0;
  const size_t byte_count = tx_scratch_.size() * sizeof(int16_t);
  const esp_err_t result =
      i2s_channel_write(static_cast<i2s_chan_handle_t>(tx_handle_),
                        tx_scratch_.data(), byte_count, &bytes_written,
                        timeout_ms);
  const bool ok = result == ESP_OK && bytes_written == byte_count;
  if (!ok) {
    static uint32_t s_write_failures = 0;
    s_write_failures += 1;
    if (s_write_failures <= 8 || (s_write_failures % 100) == 0) {
      ESP_LOGW(kLogTag,
               "write_output failed count=%" PRIu32 " result=%s bytes=%u requested=%u samples=%u timeout_ms=%" PRIu32,
               s_write_failures, esp_err_to_name(result),
               static_cast<unsigned>(bytes_written),
               static_cast<unsigned>(byte_count),
               static_cast<unsigned>(sample_count), timeout_ms);
    }
  }
  return ok;
}

bool I2sPcmAudioDevice::initialize_locked() {
  if (initialized_) {
    return true;
  }

  if (!config_.input.enabled && !config_.output.enabled) {
    ESP_LOGE(kLogTag, "no input or output channel is enabled");
    return false;
  }

  if (config_.input.enabled && config_.output.enabled &&
      config_.input.port == config_.output.port &&
      config_.input.pins.bclk == config_.output.pins.bclk &&
      config_.input.pins.ws == config_.output.pins.ws) {
    if (!initialize_duplex_channels()) {
      deinitialize_locked();
      return false;
    }
    initialized_ = true;
    return true;
  }

  if (config_.input.enabled &&
      !initialize_channel(config_.input, false, rx_handle_)) {
    deinitialize_locked();
    return false;
  }

  if (config_.output.enabled &&
      !initialize_channel(config_.output, true, tx_handle_)) {
    deinitialize_locked();
    return false;
  }

  initialized_ = true;
  return true;
}

void I2sPcmAudioDevice::deinitialize_locked() {
  input_requested_ = false;
  output_requested_ = false;
  set_channel_enabled(rx_handle_, input_enabled_, false);
  set_channel_enabled(tx_handle_, output_enabled_, false);

  if (rx_handle_ != nullptr) {
    i2s_del_channel(static_cast<i2s_chan_handle_t>(rx_handle_));
    rx_handle_ = nullptr;
  }
  if (tx_handle_ != nullptr) {
    i2s_del_channel(static_cast<i2s_chan_handle_t>(tx_handle_));
    tx_handle_ = nullptr;
  }

  device_i2s_audio_release(&config_.i2s_audio_claim);
  initialized_ = false;
  shared_duplex_clocks_ = false;
  output_uses_32bit_slots_ = false;
}

bool I2sPcmAudioDevice::initialize_duplex_channels() {
  if (config_.input.sample_rate != config_.output.sample_rate) {
    ESP_LOGE(kLogTag,
             "duplex I2S channels require matching sample rates: input=%" PRIu32 " output=%" PRIu32,
             config_.input.sample_rate, config_.output.sample_rate);
    return false;
  }

  i2s_chan_handle_t tx_handle = nullptr;
  i2s_chan_handle_t rx_handle = nullptr;
  i2s_chan_config_t channel_config =
      I2S_CHANNEL_DEFAULT_CONFIG(config_.input.port, I2S_ROLE_MASTER);
  channel_config.dma_desc_num =
      std::max(config_.input.dma_desc_num, config_.output.dma_desc_num);
  channel_config.dma_frame_num =
      std::max(config_.input.dma_frame_num, config_.output.dma_frame_num);

  esp_err_t result = i2s_new_channel(&channel_config, &tx_handle, &rx_handle);
  if (result != ESP_OK) {
    ESP_LOGE(kLogTag, "failed to create duplex channels: %s",
             esp_err_to_name(result));
    return false;
  }

  i2s_std_config_t rx_config = {};
  rx_config.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(config_.input.sample_rate);
  // Keep the board's proven INMP441 timing: the microphone is configured as
  // standard Philips/I2S, matching the previously validated product image.
  rx_config.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
      I2S_DATA_BIT_WIDTH_32BIT, slot_mode_for_channels(config_.input.channels));
  rx_config.slot_cfg.slot_mask = to_slot_mask(config_.input.slot_mask);
  rx_config.slot_cfg.ws_width = I2S_DATA_BIT_WIDTH_32BIT;
  rx_config.gpio_cfg = {
      .mclk = to_gpio(config_.input.pins.mclk),
      .bclk = to_gpio(config_.input.pins.bclk),
      .ws = to_gpio(config_.input.pins.ws),
      .dout = I2S_GPIO_UNUSED,
      .din = to_gpio(config_.input.pins.din),
      .invert_flags = {},
  };

  result = i2s_channel_init_std_mode(rx_handle, &rx_config);
  if (result != ESP_OK) {
    ESP_LOGE(kLogTag, "failed to initialize duplex rx channel: %s",
             esp_err_to_name(result));
    i2s_del_channel(rx_handle);
    i2s_del_channel(tx_handle);
    return false;
  }

  i2s_std_config_t tx_config = {};
  tx_config.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(config_.output.sample_rate);
  tx_config.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
      I2S_DATA_BIT_WIDTH_32BIT,
      slot_mode_for_channels(config_.output.channels));
  tx_config.slot_cfg.slot_mask = to_slot_mask(config_.output.slot_mask);
  tx_config.slot_cfg.ws_width = I2S_DATA_BIT_WIDTH_32BIT;
  tx_config.gpio_cfg = {
      .mclk = to_gpio(config_.output.pins.mclk),
      .bclk = to_gpio(config_.output.pins.bclk),
      .ws = to_gpio(config_.output.pins.ws),
      .dout = to_gpio(config_.output.pins.dout),
      .din = I2S_GPIO_UNUSED,
      .invert_flags = {},
  };

  result = i2s_channel_init_std_mode(tx_handle, &tx_config);
  if (result != ESP_OK) {
    ESP_LOGE(kLogTag, "failed to initialize duplex tx channel: %s",
             esp_err_to_name(result));
    i2s_del_channel(rx_handle);
    i2s_del_channel(tx_handle);
    return false;
  }

  tx_handle_ = tx_handle;
  rx_handle_ = rx_handle;
  shared_duplex_clocks_ = true;
  output_uses_32bit_slots_ = true;
  ESP_LOGI(kLogTag,
           "duplex channels ready port=%d rate=%" PRIu32 " bclk=%d ws=%d din=%d dout=%d",
           config_.input.port, config_.input.sample_rate,
           config_.input.pins.bclk, config_.input.pins.ws,
           config_.input.pins.din, config_.output.pins.dout);
  return true;
}

bool I2sPcmAudioDevice::reconcile_channel_requests_locked() {
  const bool want_rx =
      input_requested_ || (shared_duplex_clocks_ && output_requested_);
  const bool want_tx = output_requested_;

  if (shared_duplex_clocks_ && want_tx && !input_enabled_) {
    if (!set_channel_enabled(rx_handle_, input_enabled_, true)) {
      return false;
    }
  }
  if (!set_channel_enabled(tx_handle_, output_enabled_, want_tx)) {
    return false;
  }
  if (!set_channel_enabled(rx_handle_, input_enabled_, want_rx)) {
    return false;
  }
  return true;
}

bool I2sPcmAudioDevice::initialize_channel(
    const I2sPcmAudioChannelConfig &channel, bool transmit,
    void *&handle) {
  i2s_chan_handle_t channel_handle = nullptr;
  i2s_chan_config_t channel_config =
      I2S_CHANNEL_DEFAULT_CONFIG(channel.port, I2S_ROLE_MASTER);
  channel_config.dma_desc_num = channel.dma_desc_num;
  channel_config.dma_frame_num = channel.dma_frame_num;

  esp_err_t result = i2s_new_channel(
      &channel_config, transmit ? &channel_handle : nullptr,
      transmit ? nullptr : &channel_handle);
  if (result != ESP_OK) {
    ESP_LOGE(kLogTag, "failed to create %s channel: %s",
             transmit ? "tx" : "rx", esp_err_to_name(result));
    handle = nullptr;
    return false;
  }

  i2s_std_config_t standard_config = {};
  standard_config.clk_cfg =
      I2S_STD_CLK_DEFAULT_CONFIG(channel.sample_rate);
  const i2s_data_bit_width_t data_bit_width = I2S_DATA_BIT_WIDTH_32BIT;
  standard_config.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
      data_bit_width, slot_mode_for_channels(channel.channels));
  standard_config.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT;
  standard_config.slot_cfg.slot_mask = to_slot_mask(channel.slot_mask);
  standard_config.slot_cfg.ws_width = data_bit_width;
  standard_config.gpio_cfg = {
      .mclk = to_gpio(channel.pins.mclk),
      .bclk = to_gpio(channel.pins.bclk),
      .ws = to_gpio(channel.pins.ws),
      .dout = transmit ? to_gpio(channel.pins.dout) : I2S_GPIO_UNUSED,
      .din = transmit ? I2S_GPIO_UNUSED : to_gpio(channel.pins.din),
      .invert_flags = {},
  };

  result = i2s_channel_init_std_mode(channel_handle, &standard_config);
  if (result != ESP_OK) {
    ESP_LOGE(kLogTag, "failed to initialize %s channel: %s",
             transmit ? "tx" : "rx", esp_err_to_name(result));
    i2s_del_channel(channel_handle);
    handle = nullptr;
    return false;
  }

  handle = channel_handle;
  if (transmit) {
    output_uses_32bit_slots_ = true;
  }
  return true;
}

bool I2sPcmAudioDevice::set_channel_enabled(void *handle, bool &current,
                                            bool enable) {
  if (handle == nullptr) {
    return !enable;
  }
  if (current == enable) {
    return true;
  }

  const auto channel_handle = static_cast<i2s_chan_handle_t>(handle);
  const bool is_tx = handle == tx_handle_;
  if (is_tx && enable) {
    (void)preload_output_silence_locked();
  } else if (is_tx && !enable) {
    (void)write_output_silence_locked(config_.output.dma_frame_num,
                                      kOutputSilenceTimeoutMs);
  }

  const esp_err_t result =
      enable ? i2s_channel_enable(channel_handle)
             : i2s_channel_disable(channel_handle);
  if (result != ESP_OK) {
    ESP_LOGE(kLogTag, "failed to %s channel: %s",
             enable ? "enable" : "disable", esp_err_to_name(result));
    return false;
  }
  current = enable;
  if (is_tx && enable) {
    (void)write_output_silence_locked(config_.output.dma_frame_num,
                                      kOutputSilenceTimeoutMs);
  }
  return true;
}

bool I2sPcmAudioDevice::preload_output_silence_locked() {
  if (tx_handle_ == nullptr) {
    return false;
  }

  const int output_channels = std::max(1, config_.output.channels);
  const size_t frame_count = std::max<size_t>(
      kOutputSilenceMinFrames,
      static_cast<size_t>(config_.output.dma_desc_num) *
          static_cast<size_t>(config_.output.dma_frame_num));
  const size_t slot_count =
      frame_count * static_cast<size_t>(output_channels);
  size_t bytes_loaded = 0;
  esp_err_t result = ESP_OK;
  if (output_uses_32bit_slots_) {
    tx_scratch_32_.assign(slot_count, 0);
    result = i2s_channel_preload_data(
        static_cast<i2s_chan_handle_t>(tx_handle_), tx_scratch_32_.data(),
        tx_scratch_32_.size() * sizeof(int32_t), &bytes_loaded);
  } else {
    tx_scratch_.assign(slot_count, 0);
    result = i2s_channel_preload_data(
        static_cast<i2s_chan_handle_t>(tx_handle_), tx_scratch_.data(),
        tx_scratch_.size() * sizeof(int16_t), &bytes_loaded);
  }

  if (result != ESP_OK) {
    ESP_LOGW(kLogTag, "failed to preload output silence: %s",
             esp_err_to_name(result));
    return false;
  }
  if (bytes_loaded == 0) {
    ESP_LOGW(kLogTag, "output silence preload loaded no bytes");
    return false;
  }
  return true;
}

bool I2sPcmAudioDevice::write_output_silence_locked(size_t frame_count,
                                                    uint32_t timeout_ms) {
  if (tx_handle_ == nullptr || frame_count == 0) {
    return false;
  }

  const int output_channels = std::max(1, config_.output.channels);
  const size_t slot_count =
      frame_count * static_cast<size_t>(output_channels);
  size_t bytes_written = 0;
  esp_err_t result = ESP_OK;
  if (output_uses_32bit_slots_) {
    tx_scratch_32_.assign(slot_count, 0);
    result = i2s_channel_write(static_cast<i2s_chan_handle_t>(tx_handle_),
                               tx_scratch_32_.data(),
                               tx_scratch_32_.size() * sizeof(int32_t),
                               &bytes_written, timeout_ms);
    return result == ESP_OK &&
           bytes_written == tx_scratch_32_.size() * sizeof(int32_t);
  }

  tx_scratch_.assign(slot_count, 0);
  result = i2s_channel_write(static_cast<i2s_chan_handle_t>(tx_handle_),
                             tx_scratch_.data(),
                             tx_scratch_.size() * sizeof(int16_t),
                             &bytes_written, timeout_ms);
  return result == ESP_OK &&
         bytes_written == tx_scratch_.size() * sizeof(int16_t);
}

} // namespace lunaredu::device
