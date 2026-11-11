#include "lunaredu_lunaros/kernel/managed_task.h"
#include "smart_speaker_audio.h"
#include "audio_output_internal.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>

#include <driver/gpio.h>
#include <driver/i2s_std.h>
#include <esp_check.h>
#include <esp_log.h>
#include <esp_rom_gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <soc/gpio_sig_map.h>
#include <soc/gpio_struct.h>

#include "sdkconfig.h"
#include "smart_speaker_i2s_sync.h"
#include "smart_speaker_microphone_resampler.h"
#include "smart_speaker_lyrics.h"

namespace smart_speaker {
namespace audio_internal {

constexpr const char *kTag = "speaker_audio";
constexpr size_t kFramesPerBlock = 128;
// Keep the DMA ring large enough to cover the output task's scheduling
// jitter, but leave a contiguous internal-DMA block for SDMMC sector reads.
// There are three I2S channels on this product (left TX, right TX, mic RX),
// so every descriptor is paid for three times.
constexpr size_t kDmaDescriptorCount = 4;

constexpr TickType_t kWriteTimeout = pdMS_TO_TICKS(12);
constexpr uint32_t kDefaultFadeMs = CONFIG_LUNAREDU_SMART_SPEAKER_FADE_MS;
constexpr uint32_t kSettingsCommitDelayMs = 1500;
constexpr uint32_t kSettingsVersion = 1;
constexpr uint32_t kVoiceSampleRate = 16000;

struct AudioBlock {
  uint32_t track_generation = 0;
  uint32_t sequence = 0;
  uint16_t frames = 0;
  bool bypass_volume = false;
  int16_t left[kFramesPerBlock]{};
  int16_t right[kFramesPerBlock]{};
};

struct PersistedSettings {
  uint32_t version = kSettingsVersion;
  dsp::Settings settings{};
};

struct IsrCounters {
  volatile uint64_t completed_frames = 0;
  volatile uint32_t queue_overflows = 0;
};

std::array<AudioBlock, kBlockCount> g_blocks;

bool g_channels_enabled = false;
volatile bool g_reconfigure_pending = false;
volatile bool g_recovery_pending = false;
volatile bool g_output_control_pending = false;
volatile bool g_output_target_enabled = true;
volatile bool g_output_control_succeeded = false;
volatile bool g_right_bclk_forced_low = false;
volatile bool g_right_ws_forced_low = false;
volatile bool g_right_dout_forced_low = false;
// True only after the decoder has supplied a block and until a deliberate
// mute completes.  I2S can report an empty TX queue while an enabled channel
// is intentionally idle; that is not a playback underrun.
volatile bool g_audio_expected = false;
uint32_t g_pending_sample_rate = 44100;
uint32_t g_sequence = 0;
int32_t g_left_bus[kFramesPerBlock * 2]{};
int32_t g_right_bus[kFramesPerBlock * 2]{};
int32_t g_zero_bus[kFramesPerBlock * 2]{};
int32_t g_mic_raw[kFramesPerBlock * 2]{};
std::array<int16_t, kFramesPerBlock * 2> g_voice_stereo{};
dsp::AudioProcessor g_processor;
portMUX_TYPE g_state_lock = portMUX_INITIALIZER_UNLOCKED;
lyrics::PcmClock g_track_clock;
uint32_t g_track_clock_generation = 0;
dsp::Settings g_settings_target{};
dsp::ChannelMode g_channel_mode = dsp::ChannelMode::kStereo;
int g_volume_target = 50;
uint32_t g_settings_generation = 1;
bool g_settings_dirty = false;
uint32_t g_settings_changed_ms = 0;
bool g_mute_target = true;
volatile bool g_output_muted = true;
std::atomic<bool> g_microphone_input_enabled{false};
std::atomic<bool> g_microphone_input_requested{false};
std::atomic<bool> g_voice_output_enabled{false};
std::atomic<uint32_t> g_microphone_clock_rate{44100};
std::atomic<uint32_t> g_music_sample_rate{44100};
std::atomic<uint32_t> g_microphone_clock_generation{0};
dsp::Diagnostics g_dsp_snapshot{};
float g_expression_level = 0;
uint32_t g_expression_updated_ms = 0;
DualI2sDiagnostics g_diagnostics{};
IsrCounters g_left_isr{};
IsrCounters g_right_isr{};

uint32_t now_ms() {
  return static_cast<uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

i2s_std_slot_config_t make_slot_config() {
  i2s_std_slot_config_t slot = I2S_STD_PHILIP_SLOT_DEFAULT_CONFIG(
      I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO);
  slot.slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT;
  slot.ws_width = I2S_DATA_BIT_WIDTH_32BIT;
  slot.slot_mask = I2S_STD_SLOT_BOTH;
  return slot;
}

i2s_std_gpio_config_t make_gpio_config(int bclk, int ws, int dout) {
  i2s_std_gpio_config_t gpio{};
  gpio.mclk = GPIO_NUM_NC;
  gpio.bclk = static_cast<gpio_num_t>(bclk);
  gpio.ws = static_cast<gpio_num_t>(ws);
  gpio.dout = static_cast<gpio_num_t>(dout);
  gpio.din = GPIO_NUM_NC;
  gpio.invert_flags = {};
  return gpio;
}

i2s_std_gpio_config_t make_rx_gpio_config(int bclk, int ws, int din) {
  i2s_std_gpio_config_t gpio{};
  gpio.mclk = GPIO_NUM_NC;
  gpio.bclk = static_cast<gpio_num_t>(bclk);
  gpio.ws = static_cast<gpio_num_t>(ws);
  gpio.dout = GPIO_NUM_NC;
  gpio.din = static_cast<gpio_num_t>(din);
  gpio.invert_flags = {};
  return gpio;
}

void force_gpio_low(int gpio) {
  gpio_config_t config{};
  config.pin_bit_mask = 1ULL << gpio;
  config.mode = GPIO_MODE_OUTPUT;
  config.pull_up_en = GPIO_PULLUP_DISABLE;
  config.pull_down_en = GPIO_PULLDOWN_ENABLE;
  config.intr_type = GPIO_INTR_DISABLE;
  (void)gpio_config(&config);
  esp_rom_gpio_connect_out_signal(static_cast<uint32_t>(gpio),
                                  SIG_GPIO_OUT_IDX, false, false);
  (void)gpio_set_level(static_cast<gpio_num_t>(gpio), 0);
}

bool IRAM_ATTR on_sent(i2s_chan_handle_t, i2s_event_data_t *event,
                       void *context) {
  auto *counter = static_cast<IsrCounters *>(context);
  if (counter != nullptr && event != nullptr) {
    __atomic_fetch_add(&counter->completed_frames,
                       event->size / (sizeof(int32_t) * 2), __ATOMIC_RELAXED);
  }
  return false;
}

bool IRAM_ATTR on_send_q_ovf(i2s_chan_handle_t, i2s_event_data_t *,
                             void *context) {
  if (__atomic_load_n(&g_audio_expected, __ATOMIC_RELAXED)) {
    auto *counter = static_cast<IsrCounters *>(context);
    if (counter != nullptr)
      __atomic_fetch_add(&counter->queue_overflows, 1U, __ATOMIC_RELAXED);
  }
  // ESP-IDF raises this when its completed-DMA-buffer notification queue is
  // full and replaces its oldest notification. It is useful telemetry, but it
  // is not by itself proof that either I2S controller stopped. The output task
  // detects actual PCM starvation and one-sided DMA progress stalls.
  return false;
}

esp_err_t create_channel(int port, int bclk, int ws, int dout,
                         IsrCounters *counters, i2s_chan_handle_t *handle) {
  i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(port, I2S_ROLE_MASTER);
  channel.dma_desc_num = kDmaDescriptorCount;
  channel.dma_frame_num = kFramesPerBlock;
  channel.auto_clear = true;
  ESP_RETURN_ON_ERROR(i2s_new_channel(&channel, handle, nullptr), kTag,
                      "new I2S%d channel", static_cast<int>(port));
  i2s_std_config_t standard{};
  standard.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(44100);
  standard.clk_cfg.clk_src = I2S_CLK_SRC_DEFAULT;
  standard.slot_cfg = make_slot_config();
  standard.gpio_cfg = make_gpio_config(bclk, ws, dout);
  ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(*handle, &standard), kTag,
                      "init I2S%d", static_cast<int>(port));
  const i2s_event_callbacks_t callbacks = {
      .on_recv = nullptr,
      .on_recv_q_ovf = nullptr,
      .on_sent = on_sent,
      .on_send_q_ovf = on_send_q_ovf,
  };
  return i2s_channel_register_event_callback(*handle, &callbacks, counters);
}

esp_err_t create_left_duplex_channel() {
  i2s_chan_config_t channel =
      I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  channel.dma_desc_num = kDmaDescriptorCount;
  channel.dma_frame_num = kFramesPerBlock;
  channel.auto_clear = true;
  ESP_RETURN_ON_ERROR(i2s_new_channel(&channel, &g_left_tx, &g_left_rx), kTag,
                      "new I2S0 duplex channel");

  i2s_std_config_t tx_standard{};
  tx_standard.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(44100);
  tx_standard.clk_cfg.clk_src = I2S_CLK_SRC_DEFAULT;
  tx_standard.slot_cfg = make_slot_config();
  tx_standard.gpio_cfg = make_gpio_config(
      CONFIG_LUNAREDU_SMART_SPEAKER_LEFT_I2S_BCLK,
      CONFIG_LUNAREDU_SMART_SPEAKER_LEFT_I2S_WS,
      CONFIG_LUNAREDU_SMART_SPEAKER_LEFT_I2S_DOUT);
  ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(g_left_tx, &tx_standard), kTag,
                      "init I2S0 TX");

  i2s_std_config_t rx_standard = tx_standard;
  rx_standard.gpio_cfg = make_rx_gpio_config(
      CONFIG_LUNAREDU_SMART_SPEAKER_LEFT_I2S_BCLK,
      CONFIG_LUNAREDU_SMART_SPEAKER_LEFT_I2S_WS,
      CONFIG_LUNAREDU_SMART_SPEAKER_MIC_I2S_DIN);
  ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(g_left_rx, &rx_standard), kTag,
                      "init I2S0 RX");

  const i2s_event_callbacks_t callbacks = {
      .on_recv = nullptr,
      .on_recv_q_ovf = nullptr,
      .on_sent = on_sent,
      .on_send_q_ovf = on_send_q_ovf,
  };
  return i2s_channel_register_event_callback(g_left_tx, &callbacks,
                                             &g_left_isr);
}

void drain_ready_queue() {
  if (g_ready_queue == nullptr || g_free_queue == nullptr) return;
  uint8_t index = 0;
  while (xQueueReceive(g_ready_queue, &index, 0) == pdTRUE)
    (void)xQueueSend(g_free_queue, &index, 0);
}

esp_err_t stop_channels() {
  g_microphone_clock_generation.fetch_add(1, std::memory_order_acq_rel);
  esp_err_t result = ESP_OK;
  if (g_channels_enabled) {
    if (g_microphone_input_enabled && g_left_rx != nullptr) {
      const esp_err_t rx = i2s_channel_disable(g_left_rx);
      if (rx != ESP_OK && rx != ESP_ERR_INVALID_STATE) result = rx;
      g_microphone_input_enabled = false;
    }
    const esp_err_t left = i2s_channel_disable(g_left_tx);
    const esp_err_t right = i2s_channel_disable(g_right_tx);
    if (left != ESP_OK && left != ESP_ERR_INVALID_STATE) result = left;
    if (right != ESP_OK && right != ESP_ERR_INVALID_STATE) result = right;
    g_channels_enabled = false;
  }
  return result;
}

esp_err_t configure_and_start_channels(uint32_t rate) {
  ESP_RETURN_ON_ERROR(stop_channels(), kTag, "disable dual I2S");
  const i2s_std_clk_config_t clock = I2S_STD_CLK_DEFAULT_CONFIG(rate);
  const i2s_std_slot_config_t slot = make_slot_config();
  ESP_RETURN_ON_ERROR(i2s_channel_reconfig_std_clock(g_left_tx, &clock), kTag,
                      "left clock");
  if (g_left_rx != nullptr)
    ESP_RETURN_ON_ERROR(i2s_channel_reconfig_std_clock(g_left_rx, &clock),
                        kTag, "left rx clock");
  ESP_RETURN_ON_ERROR(i2s_channel_reconfig_std_clock(g_right_tx, &clock), kTag,
                      "right clock");
  ESP_RETURN_ON_ERROR(i2s_channel_reconfig_std_slot(g_left_tx, &slot), kTag,
                      "left slot");
  if (g_left_rx != nullptr)
    ESP_RETURN_ON_ERROR(i2s_channel_reconfig_std_slot(g_left_rx, &slot), kTag,
                        "left rx slot");
  ESP_RETURN_ON_ERROR(i2s_channel_reconfig_std_slot(g_right_tx, &slot), kTag,
                      "right slot");
  const i2s_std_gpio_config_t left_gpio = make_gpio_config(
      CONFIG_LUNAREDU_SMART_SPEAKER_LEFT_I2S_BCLK,
      CONFIG_LUNAREDU_SMART_SPEAKER_LEFT_I2S_WS,
      CONFIG_LUNAREDU_SMART_SPEAKER_LEFT_I2S_DOUT);
  const bool right_bclk_forced_low =
      __atomic_load_n(&g_right_bclk_forced_low, __ATOMIC_RELAXED);
  const bool right_ws_forced_low =
      __atomic_load_n(&g_right_ws_forced_low, __ATOMIC_RELAXED);
  const bool right_dout_forced_low =
      __atomic_load_n(&g_right_dout_forced_low, __ATOMIC_RELAXED);
  const i2s_std_gpio_config_t right_gpio = make_gpio_config(
      right_bclk_forced_low
          ? GPIO_NUM_NC
          : CONFIG_LUNAREDU_SMART_SPEAKER_RIGHT_I2S_BCLK,
      right_ws_forced_low ? GPIO_NUM_NC
                          : CONFIG_LUNAREDU_SMART_SPEAKER_RIGHT_I2S_WS,
      right_dout_forced_low
          ? GPIO_NUM_NC
          : CONFIG_LUNAREDU_SMART_SPEAKER_RIGHT_I2S_DOUT);
  ESP_RETURN_ON_ERROR(i2s_channel_reconfig_std_gpio(g_left_tx, &left_gpio),
                      kTag, "left GPIO");
  if (g_left_rx != nullptr) {
    const i2s_std_gpio_config_t left_rx_gpio = make_rx_gpio_config(
        CONFIG_LUNAREDU_SMART_SPEAKER_LEFT_I2S_BCLK,
        CONFIG_LUNAREDU_SMART_SPEAKER_LEFT_I2S_WS,
        CONFIG_LUNAREDU_SMART_SPEAKER_MIC_I2S_DIN);
    ESP_RETURN_ON_ERROR(i2s_channel_reconfig_std_gpio(g_left_rx,
                                                      &left_rx_gpio),
                        kTag, "left RX GPIO");
  }
  ESP_RETURN_ON_ERROR(i2s_channel_reconfig_std_gpio(g_right_tx, &right_gpio),
                      kTag, "right GPIO");
  if (right_bclk_forced_low)
    force_gpio_low(CONFIG_LUNAREDU_SMART_SPEAKER_RIGHT_I2S_BCLK);
  if (right_ws_forced_low)
    force_gpio_low(CONFIG_LUNAREDU_SMART_SPEAKER_RIGHT_I2S_WS);
  if (right_dout_forced_low)
    force_gpio_low(CONFIG_LUNAREDU_SMART_SPEAKER_RIGHT_I2S_DOUT);
  size_t left_loaded = 0;
  size_t right_loaded = 0;
  ESP_RETURN_ON_ERROR(i2s_channel_preload_data(
                          g_left_tx, g_zero_bus, sizeof(g_zero_bus), &left_loaded),
                      kTag, "left zero preload");
  ESP_RETURN_ON_ERROR(i2s_channel_preload_data(g_right_tx, g_zero_bus,
                                                sizeof(g_zero_bus),
                                                &right_loaded),
                      kTag, "right zero preload");
  if (left_loaded != right_loaded) return ESP_ERR_INVALID_SIZE;
  ESP_RETURN_ON_ERROR(i2s_channel_enable(g_left_tx), kTag, "left enable");
  const esp_err_t right_enable = i2s_channel_enable(g_right_tx);
  if (right_enable != ESP_OK) {
    (void)i2s_channel_disable(g_left_tx);
    return right_enable;
  }

  i2s_chan_info_t left_info{};
  i2s_chan_info_t right_info{};
  const esp_err_t left_info_err = i2s_channel_get_info(g_left_tx, &left_info);
  const esp_err_t right_info_err =
      i2s_channel_get_info(g_right_tx, &right_info);
  if (left_info_err != ESP_OK || right_info_err != ESP_OK) {
    (void)i2s_channel_disable(g_right_tx);
    (void)i2s_channel_disable(g_left_tx);
    return left_info_err != ESP_OK ? left_info_err : right_info_err;
  }
  g_diagnostics.left_bclk_hz = left_info.bclk_hz;
  g_diagnostics.right_bclk_hz = right_info.bclk_hz;
  g_diagnostics.left_mclk_hz = left_info.mclk_hz;
  g_diagnostics.right_mclk_hz = right_info.mclk_hz;
  g_diagnostics.left_clock_source = static_cast<int>(left_info.clk_src);
  g_diagnostics.right_clock_source = static_cast<int>(right_info.clk_src);
  g_diagnostics.clocks_match =
      left_info.bclk_hz == right_info.bclk_hz &&
      left_info.mclk_hz == right_info.mclk_hz &&
      left_info.clk_src == right_info.clk_src;
  if (!g_diagnostics.clocks_match) {
    (void)i2s_channel_disable(g_right_tx);
    (void)i2s_channel_disable(g_left_tx);
    return ESP_ERR_INVALID_STATE;
  }
  ESP_LOGI(kTag, "[DEBUG-speaker-io] matrix bclk0=%lu ws0=%lu expected_tx=%d/%d rx=%d/%d",
      static_cast<unsigned long>(GPIO.func_out_sel_cfg[CONFIG_LUNAREDU_SMART_SPEAKER_LEFT_I2S_BCLK].func_sel),
      static_cast<unsigned long>(GPIO.func_out_sel_cfg[CONFIG_LUNAREDU_SMART_SPEAKER_LEFT_I2S_WS].func_sel),
      I2S0O_BCK_OUT_IDX, I2S0O_WS_OUT_IDX, I2S0I_BCK_OUT_IDX, I2S0I_WS_OUT_IDX);
  g_channels_enabled = true;
  g_diagnostics.output_enabled = true;
  g_diagnostics.microphone_input_enabled = g_microphone_input_enabled;
  g_diagnostics.right_bclk_forced_low = right_bclk_forced_low;
  g_diagnostics.right_ws_forced_low = right_ws_forced_low;
  g_diagnostics.right_dout_forced_low = right_dout_forced_low;
  g_microphone_clock_rate.store(rate, std::memory_order_release);
  if (g_microphone_input_requested.load(std::memory_order_acquire) && g_left_rx != nullptr) {
    const esp_err_t rx = i2s_channel_enable(g_left_rx);
    if (rx != ESP_OK && rx != ESP_ERR_INVALID_STATE) return rx;
    g_microphone_input_enabled.store(true, std::memory_order_release);
    g_diagnostics.microphone_input_enabled = true;
  }
  g_microphone_clock_generation.fetch_add(1, std::memory_order_acq_rel);
  g_diagnostics.sample_rate = rate;
  g_diagnostics.synchronized = true;
  ESP_LOGI(kTag,
           "dual I2S started rate=%lu bclk=%lu mclk=%lu source=%d preload=%u "
           "(software-adjacent enable)",
           static_cast<unsigned long>(rate),
           static_cast<unsigned long>(left_info.bclk_hz),
           static_cast<unsigned long>(left_info.mclk_hz),
           static_cast<int>(left_info.clk_src),
           static_cast<unsigned>(left_loaded));
  return ESP_OK;
}

void mark_fault(const char *reason) {
  g_diagnostics.fault_muted = true;
  g_diagnostics.synchronized = false;
  g_recovery_pending = true;
  portENTER_CRITICAL(&g_state_lock);
  g_mute_target = true;
  ++g_settings_generation;
  portEXIT_CRITICAL(&g_state_lock);
  ESP_LOGE(kTag, "dual I2S fault: %s; muting and resynchronizing", reason);
}

void apply_control_targets(uint32_t &seen_generation) {
  dsp::Settings settings;
  dsp::ChannelMode mode;
  int volume;
  bool mute;
  uint32_t generation;
  portENTER_CRITICAL(&g_state_lock);
  settings = g_settings_target;
  mode = g_channel_mode;
  volume = g_volume_target;
  mute = g_mute_target;
  generation = g_settings_generation;
  portEXIT_CRITICAL(&g_state_lock);
  if (generation != seen_generation) {
    g_processor.set_settings(settings);
    g_processor.set_channel_mode(mode);
    g_processor.set_volume(volume);
    if (mute) {
      __atomic_store_n(&g_audio_expected, false, __ATOMIC_RELAXED);
      g_processor.request_mute(kDefaultFadeMs);
    }
    else g_processor.request_unmute(kDefaultFadeMs);
    seen_generation = generation;
  }
}

void do_recovery_or_reconfigure() {
  const bool was_reconfigure = g_reconfigure_pending;
  const uint32_t rate = g_pending_sample_rate;
  g_processor.request_mute(1);
  __atomic_store_n(&g_audio_expected, false, __ATOMIC_RELAXED);
  drain_ready_queue();
  (void)stop_channels();
  const esp_err_t err = configure_and_start_channels(rate);
  if (err == ESP_OK && g_processor.configure(rate, dual_i2s_audio_settings())) {
    g_processor.set_volume(g_volume_target);
    g_processor.set_channel_mode(g_channel_mode);
    g_diagnostics.fault_muted = false;
    g_diagnostics.synchronized = true;
    ++g_diagnostics.resync_count;
    if (!was_reconfigure) {
      portENTER_CRITICAL(&g_state_lock);
      g_mute_target = false;
      ++g_settings_generation;
      portEXIT_CRITICAL(&g_state_lock);
      g_processor.request_unmute(kDefaultFadeMs);
    }
  } else {
    g_diagnostics.fault_muted = true;
    g_diagnostics.synchronized = false;
  }
  g_recovery_pending = false;
  g_reconfigure_pending = false;
  if (was_reconfigure && g_reconfigure_done != nullptr)
    xSemaphoreGive(g_reconfigure_done);
}

void output_task(void *) {
  uint32_t seen_generation = 0;
  uint32_t last_block_ms = now_ms();
  uint32_t last_write_started_ms = 0;
  bool stream_active = false;
  I2sCompletionMonitor completion_monitor;
  completion_monitor.reset(
      __atomic_load_n(&g_left_isr.completed_frames, __ATOMIC_RELAXED),
      __atomic_load_n(&g_right_isr.completed_frames, __ATOMIC_RELAXED),
      now_ms());
  for (;;) {
    if (g_output_stop.load(std::memory_order_acquire)) {
      g_output_exited.store(true, std::memory_order_release);
      return;
    }
    apply_control_targets(seen_generation);
    if (g_output_control_pending) {
      const bool enable = g_output_target_enabled;
      esp_err_t err = ESP_OK;
      if (enable) {
        err = configure_and_start_channels(g_pending_sample_rate);
      } else {
        err = stop_channels();
        smart_speaker_quiet_audio_pins_early();
        g_diagnostics.output_enabled = false;
        g_diagnostics.synchronized = false;
        g_diagnostics.clocks_match = false;
      }
      if (err != ESP_OK) {
        g_diagnostics.fault_muted = true;
        g_diagnostics.synchronized = false;
        g_diagnostics.output_enabled = false;
        ESP_LOGE(kTag, "hardware output %s failed: %s",
                 enable ? "enable" : "disable", esp_err_to_name(err));
      } else {
        ESP_LOGW(kTag, "hardware output %s; I2S pins %s",
                 enable ? "enabled" : "disabled",
                 enable ? "restored" : "forced low");
      }
      g_output_control_succeeded = err == ESP_OK;
      g_output_control_pending = false;
      stream_active = false;
      completion_monitor.reset(
          __atomic_load_n(&g_left_isr.completed_frames, __ATOMIC_RELAXED),
          __atomic_load_n(&g_right_isr.completed_frames, __ATOMIC_RELAXED),
          now_ms());
      if (g_output_control_done != nullptr) xSemaphoreGive(g_output_control_done);
      continue;
    }
    const uint64_t completed_left =
        __atomic_load_n(&g_left_isr.completed_frames, __ATOMIC_RELAXED);
    const uint64_t completed_right =
        __atomic_load_n(&g_right_isr.completed_frames, __ATOMIC_RELAXED);
    const bool completion_monitor_active =
        stream_active && g_channels_enabled &&
        __atomic_load_n(&g_audio_expected, __ATOMIC_RELAXED) &&
        !g_processor.muted();
    if (!g_recovery_pending && !g_reconfigure_pending) {
      switch (completion_monitor.observe(completed_left, completed_right,
                                         now_ms(), completion_monitor_active)) {
        case I2sCompletionFault::kLeftStalled:
          ++g_diagnostics.completion_stalls_left;
          mark_fault("left DMA completion stalled");
          break;
        case I2sCompletionFault::kRightStalled:
          ++g_diagnostics.completion_stalls_right;
          mark_fault("right DMA completion stalled");
          break;
        case I2sCompletionFault::kNone:
          break;
      }
    }
    if (g_recovery_pending || g_reconfigure_pending) {
      do_recovery_or_reconfigure();
      last_block_ms = now_ms();
      stream_active = false;
      completion_monitor.reset(
          __atomic_load_n(&g_left_isr.completed_frames, __ATOMIC_RELAXED),
          __atomic_load_n(&g_right_isr.completed_frames, __ATOMIC_RELAXED),
          now_ms());
    }

    uint8_t index = 0;
    if (xQueueReceive(g_ready_queue, &index, pdMS_TO_TICKS(2)) != pdTRUE) {
      if (g_processor.fade_state() == dsp::FadeState::kRampDown) {
        // Advance the state even if a decoder ends immediately after mute().
        for (size_t i = 0; i < kFramesPerBlock; ++i) {
          float left = 0.0f;
          float right = 0.0f;
          g_processor.finalize_output(left, right);
        }
      }
      if (g_processor.muted()) {
        stream_active = false;
        __atomic_store_n(&g_audio_expected, false, __ATOMIC_RELAXED);
      }
      g_output_muted = g_processor.muted();
      if (stream_active &&
          __atomic_load_n(&g_audio_expected, __ATOMIC_RELAXED) &&
          !g_processor.muted() &&
          now_ms() - last_block_ms > 20) {
        ++g_diagnostics.inferred_underruns;
        mark_fault("PCM queue underrun");
      }
      continue;
    }
    AudioBlock &block = g_blocks[index];
    stream_active = true;
    bool unmuted_target = false;
    portENTER_CRITICAL(&g_state_lock);
    unmuted_target = !g_mute_target;
    portEXIT_CRITICAL(&g_state_lock);
    // Blocks already queued when pause/stop requests mute must not turn DMA
    // starvation monitoring back on. Only a block belonging to an unmuted
    // playback target proves that continuous PCM is expected.
    if (unmuted_target)
      __atomic_store_n(&g_audio_expected, true, __ATOMIC_RELAXED);
    last_block_ms = now_ms();
    float expression_peak = 0;
    uint32_t probe_peak0 = 0, probe_peak1 = 0;
    for (size_t i = 0; i < block.frames; ++i) {
      float left = 0.0f;
      float right = 0.0f;
      float pre_volume = 0.0f;
      g_processor.process_common(block.left[i], block.right[i], left, right,
                                 pre_volume, !block.bypass_volume);
      expression_peak = std::max(expression_peak, std::abs(pre_volume));
      g_processor.finalize_output(left, right);
      // No FFT/VU screen remains. Keep the DSP/output path, but avoid the
      // legacy per-sample spectrum buffer writes and critical section.
      const int16_t left16 = g_processor.quantize(left);
      const int16_t right16 = g_processor.quantize(right);
      probe_peak0 = std::max(probe_peak0, static_cast<uint32_t>(std::abs(static_cast<int>(right16))));
      probe_peak1 = std::max(probe_peak1, static_cast<uint32_t>(std::abs(static_cast<int>(left16))));
      const int32_t left32 = static_cast<int32_t>(
          static_cast<uint32_t>(static_cast<uint16_t>(left16)) << 16);
      const int32_t right32 = static_cast<int32_t>(
          static_cast<uint32_t>(static_cast<uint16_t>(right16)) << 16);
      // I2S0 is the full-duplex bus shared by the physical right speaker and
      // microphone. I2S1 drives the physical left speaker.
      g_left_bus[i * 2] = right32;
      g_left_bus[i * 2 + 1] = right32;
      g_right_bus[i * 2] = left32;
      g_right_bus[i * 2 + 1] = left32;
    }
    __atomic_store_n(&g_diagnostics.probe_peak_i2s0,
        std::max(__atomic_load_n(&g_diagnostics.probe_peak_i2s0, __ATOMIC_RELAXED), probe_peak0), __ATOMIC_RELAXED);
    __atomic_store_n(&g_diagnostics.probe_peak_i2s1,
        std::max(__atomic_load_n(&g_diagnostics.probe_peak_i2s1, __ATOMIC_RELAXED), probe_peak1), __ATOMIC_RELAXED);
    g_output_muted = g_processor.muted();
    portENTER_CRITICAL(&g_state_lock);
    g_dsp_snapshot = g_processor.diagnostics();
    // Pre-volume energy makes expression independent of knob position. Publish
    // once per PCM block, using the existing lock; no per-sample synchronization.
    g_expression_level = std::max(expression_peak, g_expression_level * .94f);
    g_expression_updated_ms = last_block_ms;
    portEXIT_CRITICAL(&g_state_lock);
    const size_t wanted = block.frames * sizeof(int32_t) * 2;
    size_t left_written = 0;
    size_t right_written = 0;
    const uint32_t started = now_ms();
    if (last_write_started_ms != 0)
      g_diagnostics.maximum_write_gap_ms = std::max(
          g_diagnostics.maximum_write_gap_ms, started - last_write_started_ms);
    last_write_started_ms = started;
    const esp_err_t left_err = i2s_channel_write(
        g_left_tx, g_left_bus, wanted, &left_written, kWriteTimeout);
    const esp_err_t right_err = i2s_channel_write(
        g_right_tx, g_right_bus, wanted, &right_written, kWriteTimeout);
    const uint32_t elapsed = now_ms() - started;
    g_diagnostics.maximum_write_time_ms =
        std::max(g_diagnostics.maximum_write_time_ms, elapsed);
    if (left_err == ESP_ERR_TIMEOUT) ++g_diagnostics.timeouts_left;
    if (right_err == ESP_ERR_TIMEOUT) ++g_diagnostics.timeouts_right;
    if (left_written != wanted) ++g_diagnostics.short_writes_left;
    if (right_written != wanted) ++g_diagnostics.short_writes_right;
    if (left_err != ESP_OK || right_err != ESP_OK || left_written != wanted ||
        right_written != wanted) {
      mark_fault("short write or timeout");
    } else {
      portENTER_CRITICAL(&g_state_lock);
      if (block.track_generation == g_track_clock_generation)
        g_track_clock.advance(block.frames, g_diagnostics.sample_rate);
      portEXIT_CRITICAL(&g_state_lock);
      g_diagnostics.submitted_frames_left += block.frames;
      g_diagnostics.submitted_frames_right += block.frames;
      if (g_diagnostics.submitted_frames_left !=
          g_diagnostics.submitted_frames_right)
        mark_fault("submitted frame mismatch");
    }
    (void)xQueueSend(g_free_queue, &index, 0);
  }
}

void load_settings() {
  g_settings_target = dsp::preset_settings(dsp::Preset::kFlat);
  nvs_handle_t nvs = 0;
  if (nvs_open("spkr_dsp", NVS_READONLY, &nvs) != ESP_OK) return;
  PersistedSettings stored;
  size_t size = sizeof(stored);
  if (nvs_get_blob(nvs, "settings", &stored, &size) == ESP_OK &&
      size == sizeof(stored) && stored.version == kSettingsVersion &&
      dsp::validate_settings(stored.settings)) {
    g_settings_target = stored.settings;
  }
  nvs_close(nvs);
}

bool prepare_audio_pipeline() {
  load_settings();
  if (create_left_duplex_channel() != ESP_OK ||
      create_channel(I2S_NUM_1, CONFIG_LUNAREDU_SMART_SPEAKER_RIGHT_I2S_BCLK,
                     CONFIG_LUNAREDU_SMART_SPEAKER_RIGHT_I2S_WS,
                     CONFIG_LUNAREDU_SMART_SPEAKER_RIGHT_I2S_DOUT, &g_right_isr,
                     &g_right_tx) != ESP_OK)
    return false;
  if (!g_processor.configure(44100, g_settings_target)) return false;
  g_processor.set_volume(g_volume_target);
  if (configure_and_start_channels(44100) != ESP_OK) return false;
  return true;
}
void reset_audio_control() {
  g_microphone_input_enabled = false;
  g_microphone_input_requested = false;
  g_voice_output_enabled = false;
  g_microphone_clock_rate = g_music_sample_rate = 44100;
  g_microphone_clock_generation.fetch_add(1, std::memory_order_acq_rel);
  g_channels_enabled=false;
  g_reconfigure_pending = false;
  g_recovery_pending = false;
  g_output_control_pending = false;
  g_output_target_enabled = true;
  g_output_control_succeeded = false;
  g_pending_sample_rate = 44100;
  g_output_muted = true;
  __atomic_store_n(&g_audio_expected, false, __ATOMIC_RELAXED);
  g_diagnostics = {};
}
} // namespace audio_internal
using namespace audio_internal;

void smart_speaker_quiet_audio_pins_early() {
  const uint64_t mask = (1ULL << CONFIG_LUNAREDU_SMART_SPEAKER_LEFT_I2S_WS) |
                        (1ULL << CONFIG_LUNAREDU_SMART_SPEAKER_LEFT_I2S_BCLK) |
                        (1ULL << CONFIG_LUNAREDU_SMART_SPEAKER_LEFT_I2S_DOUT) |
                        (1ULL << CONFIG_LUNAREDU_SMART_SPEAKER_RIGHT_I2S_WS) |
                        (1ULL << CONFIG_LUNAREDU_SMART_SPEAKER_RIGHT_I2S_BCLK) |
                        (1ULL << CONFIG_LUNAREDU_SMART_SPEAKER_RIGHT_I2S_DOUT);
  gpio_config_t config{};
  config.pin_bit_mask = mask;
  config.mode = GPIO_MODE_OUTPUT;
  config.pull_up_en = GPIO_PULLUP_DISABLE;
  config.pull_down_en = GPIO_PULLDOWN_ENABLE;
  config.intr_type = GPIO_INTR_DISABLE;
  (void)gpio_config(&config);
  for (int gpio : {CONFIG_LUNAREDU_SMART_SPEAKER_LEFT_I2S_WS,
                   CONFIG_LUNAREDU_SMART_SPEAKER_LEFT_I2S_BCLK,
                   CONFIG_LUNAREDU_SMART_SPEAKER_LEFT_I2S_DOUT,
                   CONFIG_LUNAREDU_SMART_SPEAKER_RIGHT_I2S_WS,
                   CONFIG_LUNAREDU_SMART_SPEAKER_RIGHT_I2S_BCLK,
                   CONFIG_LUNAREDU_SMART_SPEAKER_RIGHT_I2S_DOUT}) {
    esp_rom_gpio_connect_out_signal(static_cast<uint32_t>(gpio),
                                    SIG_GPIO_OUT_IDX, false, false);
    (void)gpio_set_level(static_cast<gpio_num_t>(gpio), 0);
  }
}

bool smart_speaker_validate_and_log_gpio_ownership() {
  struct Pin { const char *owner; int gpio; };
  const Pin pins[] = {
      {"lcd.cs", CONFIG_LUNAREDU_DEVICE_DISPLAY_SPI_LCD_CS},
      {"lcd.dc", CONFIG_LUNAREDU_DEVICE_DISPLAY_SPI_LCD_DC},
      {"lcd.mosi", CONFIG_LUNAREDU_DEVICE_DISPLAY_SPI_LCD_MOSI},
      {"lcd.sclk", CONFIG_LUNAREDU_DEVICE_DISPLAY_SPI_LCD_SCLK},
      {"touch.sda", CONFIG_LUNAREDU_SMART_SPEAKER_TOUCH_SDA_GPIO},
      {"touch.scl", CONFIG_LUNAREDU_SMART_SPEAKER_TOUCH_SCL_GPIO},
      {"touch.int", CONFIG_LUNAREDU_SMART_SPEAKER_TOUCH_INT_GPIO},
      {"mpu.sda", CONFIG_LUNAREDU_SMART_SPEAKER_MPU6050_SDA_GPIO},
      {"mpu.scl", CONFIG_LUNAREDU_SMART_SPEAKER_MPU6050_SCL_GPIO},
      {"encoder.a", CONFIG_LUNAREDU_SMART_SPEAKER_ENCODER_A_GPIO},
      {"encoder.b", CONFIG_LUNAREDU_SMART_SPEAKER_ENCODER_B_GPIO},
      {"button.left", CONFIG_LUNAREDU_SMART_SPEAKER_LEFT_BUTTON_GPIO},
      {"button.right", CONFIG_LUNAREDU_SMART_SPEAKER_RIGHT_BUTTON_GPIO},
      {"i2s0.right.ws", CONFIG_LUNAREDU_SMART_SPEAKER_LEFT_I2S_WS},
      {"i2s0.right.bclk", CONFIG_LUNAREDU_SMART_SPEAKER_LEFT_I2S_BCLK},
      {"i2s0.right.dout", CONFIG_LUNAREDU_SMART_SPEAKER_LEFT_I2S_DOUT},
      {"i2s0.din", CONFIG_LUNAREDU_SMART_SPEAKER_MIC_I2S_DIN},
      {"i2s1.left.ws", CONFIG_LUNAREDU_SMART_SPEAKER_RIGHT_I2S_WS},
      {"i2s1.left.bclk", CONFIG_LUNAREDU_SMART_SPEAKER_RIGHT_I2S_BCLK},
      {"i2s1.left.dout", CONFIG_LUNAREDU_SMART_SPEAKER_RIGHT_I2S_DOUT},
      {"sd.cmd", 38}, {"sd.clk", 39}, {"sd.d0", 40},
  };
  bool unique = true;
  for (size_t i = 0; i < std::size(pins); ++i) {
    ESP_LOGI(kTag, "GPIO owner %-14s -> %d", pins[i].owner, pins[i].gpio);
    for (size_t j = i + 1; j < std::size(pins); ++j) {
      if (pins[i].gpio == pins[j].gpio) {
        ESP_LOGE(kTag, "GPIO conflict: %s and %s on %d", pins[i].owner,
                 pins[j].owner, pins[i].gpio);
        unique = false;
      }
    }
  }
  ESP_LOGW(kTag, "GPIO47/14 are shared by right MAX98357 and I2S microphone");
  return unique;
}

void dual_i2s_track_clock_reset() {
  portENTER_CRITICAL(&g_state_lock);
  ++g_track_clock_generation;
  g_track_clock.reset();
  portEXIT_CRITICAL(&g_state_lock);
}
uint32_t dual_i2s_track_position_ms() {
  portENTER_CRITICAL(&g_state_lock);
  const uint32_t ms = g_track_clock.milliseconds();
  portEXIT_CRITICAL(&g_state_lock);
  return ms;
}

esp_err_t write_audio_blocks(void *audio_buffer, size_t len,
                             size_t *bytes_written, uint32_t timeout_ms,
                             bool bypass_volume) {
  if (bytes_written) *bytes_written = 0;
  if (!audio_buffer || len == 0 || len % (sizeof(int16_t) * 2) != 0) {
    ++g_diagnostics.invalid_pcm_blocks;
    dual_i2s_audio_emergency_mute();
    return ESP_ERR_INVALID_SIZE;
  }
  if (g_free_queue == nullptr || g_diagnostics.fault_muted)
    return ESP_ERR_INVALID_STATE;
  const auto *samples = static_cast<const int16_t *>(audio_buffer);
  const size_t total_frames = len / (sizeof(int16_t) * 2);
  portENTER_CRITICAL(&g_state_lock);
  const uint32_t generation = g_track_clock_generation;
  portEXIT_CRITICAL(&g_state_lock);
  size_t offset = 0;
  while (offset < total_frames) {
    uint8_t index = 0;
    const TickType_t wait = pdMS_TO_TICKS(std::min<uint32_t>(timeout_ms, 12));
    if (xQueueReceive(g_free_queue, &index, wait) != pdTRUE) {
      ++g_diagnostics.input_queue_overflows;
      mark_fault("fixed PCM block pool exhausted");
      return ESP_ERR_TIMEOUT;
    }
    AudioBlock &block = g_blocks[index];
    block.sequence = ++g_sequence;
    block.track_generation = generation;
    block.frames = static_cast<uint16_t>(
        std::min(kFramesPerBlock, total_frames - offset));
    block.bypass_volume = bypass_volume;
    for (size_t i = 0; i < block.frames; ++i) {
      block.left[i] = samples[(offset + i) * 2];
      block.right[i] = samples[(offset + i) * 2 + 1];
    }
    if (xQueueSend(g_ready_queue, &index, 0) != pdTRUE) {
      (void)xQueueSend(g_free_queue, &index, 0);
      ++g_diagnostics.input_queue_overflows;
      mark_fault("PCM ready queue overflow");
      return ESP_ERR_TIMEOUT;
    }
    offset += block.frames;
  }
  if (bytes_written) *bytes_written = len;
  return ESP_OK;
}

esp_err_t dual_i2s_audio_write(void *audio_buffer, size_t len,
                               size_t *bytes_written, uint32_t timeout_ms,
                               void *) {
  return write_audio_blocks(audio_buffer, len, bytes_written, timeout_ms,
                            false);
}

esp_err_t dual_i2s_audio_reconfigure(uint32_t rate, uint32_t bits,
                                     i2s_slot_mode_t channels) {
  if (!g_reconfigure_done || g_output_stop.load()) return ESP_ERR_INVALID_STATE;
  if (rate < 8000 || rate > 48000 || bits != 16 ||
      channels != I2S_SLOT_MODE_STEREO) {
    dual_i2s_audio_emergency_mute();
    return ESP_ERR_INVALID_ARG;
  }
  g_music_sample_rate.store(rate, std::memory_order_release);
  (void)dual_i2s_audio_fade_to_mute(100);
  g_pending_sample_rate = rate;
  g_reconfigure_pending = true;
  if (xSemaphoreTake(g_reconfigure_done, pdMS_TO_TICKS(250)) != pdTRUE)
    return ESP_ERR_TIMEOUT;
  if (!g_diagnostics.synchronized) return ESP_FAIL;
  dual_i2s_audio_fade_up();
  return ESP_OK;
}

bool smart_speaker_voice_audio_enable_input(bool enabled) {
  if (g_left_rx == nullptr) return false;
  g_microphone_input_requested.store(enabled, std::memory_order_release);
  // RX shares I2S0 clocks with music TX. Standby capture must not mute the
  // player or change its sample rate; the read boundary converts to 16 kHz.
  const esp_err_t result = enabled ? i2s_channel_enable(g_left_rx) : i2s_channel_disable(g_left_rx);
  if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
    g_microphone_input_requested.store(false, std::memory_order_release);
    return false;
  }
  g_microphone_input_enabled.store(enabled, std::memory_order_release);
  g_microphone_clock_generation.fetch_add(1, std::memory_order_acq_rel);
  g_diagnostics.microphone_input_enabled = enabled;
  return true;
}

bool smart_speaker_voice_audio_enable_output(bool enabled) {
  if (!enabled) {
    if (!g_voice_output_enabled.exchange(false, std::memory_order_acq_rel)) return true;
    (void)dual_i2s_audio_fade_to_mute(100);
    // The decoder may resume an already-open file without another clock
    // callback. Restore its latest requested rate before releasing voice focus.
    const uint32_t music_rate = g_music_sample_rate.load(std::memory_order_acquire);
    if (g_microphone_clock_rate.load(std::memory_order_acquire) != music_rate ||
        (g_reconfigure_pending && g_pending_sample_rate != music_rate)) {
      g_pending_sample_rate = music_rate;
      g_reconfigure_pending = true;
      if (g_reconfigure_done == nullptr ||
          xSemaphoreTake(g_reconfigure_done, pdMS_TO_TICKS(250)) != pdTRUE ||
          !g_diagnostics.synchronized) return false;
    }
    return true;
  }
  const bool restore_input = g_microphone_input_enabled;
  // Own a scheduled clock change even if the acknowledgement times out; a
  // later close must cancel/restore that pending change before music resumes.
  g_voice_output_enabled.store(true, std::memory_order_release);
  g_pending_sample_rate = kVoiceSampleRate;
  g_reconfigure_pending = true;
  if (g_reconfigure_done == nullptr ||
      xSemaphoreTake(g_reconfigure_done, pdMS_TO_TICKS(250)) != pdTRUE ||
      !g_diagnostics.synchronized) {
    return false;
  }
  if (restore_input && g_left_rx != nullptr) {
    const esp_err_t result = i2s_channel_enable(g_left_rx);
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) return false;
    g_microphone_input_enabled = true;
    g_diagnostics.microphone_input_enabled = true;
  }
  dual_i2s_audio_fade_up();
  return true;
}

bool smart_speaker_voice_audio_configure_output(uint32_t sample_rate) {
  if (sample_rate != kVoiceSampleRate) return false;
  return smart_speaker_voice_audio_enable_output(true);
}

bool smart_speaker_voice_audio_read_input(int16_t *samples, size_t sample_count,
                                          uint32_t timeout_ms) {
  if (samples == nullptr || sample_count == 0 || g_left_rx == nullptr ||
      !g_microphone_input_requested.load(std::memory_order_acquire)) return false;
  // Only the existing Pet audio worker calls this read boundary. Keep a
  // bounded carry buffer so DMA block boundaries never alter the sample rate.
  static MicrophoneResampler resampler;
  static std::array<int16_t, kFramesPerBlock * 2> converted{};
  static size_t pending_begin = 0, pending_end = 0;
  static uint32_t previous_generation = UINT32_MAX;
  const uint32_t generation = g_microphone_clock_generation.load(std::memory_order_acquire);
  const uint32_t rate = g_microphone_clock_rate.load(std::memory_order_acquire);
  if (rate < 8000 || rate > 48000) return false;
  if (generation != previous_generation) {
    resampler.reset(rate);
    pending_begin = pending_end = 0;
    previous_generation = generation;
  }
  const uint32_t started = now_ms();
  size_t total_read = 0;
  while (total_read < sample_count) {
    if (!g_microphone_input_requested.load(std::memory_order_acquire) ||
        generation != g_microphone_clock_generation.load(std::memory_order_acquire)) return false;
    while (pending_begin < pending_end && total_read < sample_count)
      samples[total_read++] = converted[pending_begin++];
    if (total_read == sample_count) break;
    const uint32_t elapsed = now_ms() - started;
    if (elapsed >= timeout_ms) return false;
    const size_t frames = std::min<size_t>(kFramesPerBlock,
        std::max<size_t>(1, ((sample_count - total_read) * rate + 15999) / 16000));
    size_t bytes_read = 0;
    const esp_err_t result = i2s_channel_read(g_left_rx, g_mic_raw,
        frames * sizeof(int32_t) * 2, &bytes_read, timeout_ms - elapsed);
    if (result != ESP_OK || bytes_read == 0 ||
        generation != g_microphone_clock_generation.load(std::memory_order_acquire)) return false;
    const size_t raw_frames = std::min(frames, bytes_read / (sizeof(int32_t) * 2));
    int32_t left_peak = 0, right_peak = 0;
    for (size_t i = 0; i < raw_frames; ++i) {
      left_peak = std::max(left_peak, std::abs(g_mic_raw[i * 2] >> 16));
      right_peak = std::max(right_peak, std::abs(g_mic_raw[i * 2 + 1] >> 16));
    }
    const size_t mic_slot = right_peak > left_peak ? 1 : 0;
    pending_begin = pending_end = 0;
    for (size_t i = 0; i < raw_frames; ++i) {
      const int32_t shifted = g_mic_raw[i * 2 + mic_slot] >> 16;
      const auto amplified = static_cast<int16_t>(std::clamp<int32_t>(shifted * 4,
          std::numeric_limits<int16_t>::min(), std::numeric_limits<int16_t>::max()));
      pending_end += resampler.push(amplified, converted.data() + pending_end);
    }
    if (!raw_frames) return false;
  }
  return true;
}

bool smart_speaker_voice_audio_write_output(const int16_t *samples,
                                            size_t sample_count,
                                            uint32_t timeout_ms) {
  if (samples == nullptr || sample_count == 0) return false;
  size_t offset = 0;
  while (offset < sample_count) {
    const size_t frames = std::min(g_voice_stereo.size() / 2,
                                   sample_count - offset);
    for (size_t i = 0; i < frames; ++i) {
      g_voice_stereo[i * 2] = samples[offset + i];
      g_voice_stereo[i * 2 + 1] = samples[offset + i];
    }
    size_t written = 0;
    const esp_err_t result = write_audio_blocks(
        g_voice_stereo.data(), frames * sizeof(int16_t) * 2, &written,
        timeout_ms, true);
    if (result != ESP_OK || written != frames * sizeof(int16_t) * 2)
      return false;
    offset += frames;
  }
  return true;
}

int smart_speaker_voice_audio_input_sample_rate() {
  return static_cast<int>(kVoiceSampleRate);
}

int smart_speaker_voice_audio_output_sample_rate() {
  return static_cast<int>(kVoiceSampleRate);
}

esp_err_t dual_i2s_audio_mute(AUDIO_PLAYER_MUTE_SETTING setting) {
  if (setting == AUDIO_PLAYER_MUTE)
    __atomic_store_n(&g_audio_expected, false, __ATOMIC_RELAXED);
  portENTER_CRITICAL(&g_state_lock);
  g_mute_target = setting == AUDIO_PLAYER_MUTE;
  ++g_settings_generation;
  portEXIT_CRITICAL(&g_state_lock);
  return ESP_OK;
}

bool dual_i2s_audio_fade_to_mute(uint32_t timeout_ms) {
  (void)dual_i2s_audio_mute(AUDIO_PLAYER_MUTE);
  const uint32_t start = now_ms();
  while (!g_output_muted && now_ms() - start < timeout_ms)
    vTaskDelay(pdMS_TO_TICKS(2));
  return g_output_muted;
}
void dual_i2s_audio_fade_up() {
  (void)dual_i2s_audio_mute(AUDIO_PLAYER_UNMUTE);
}

void dual_i2s_audio_set_volume(int volume) {
  portENTER_CRITICAL(&g_state_lock);
  g_volume_target = std::clamp(volume, 0, dsp::kSystemMaxVolume);
  ++g_settings_generation;
  portEXIT_CRITICAL(&g_state_lock);
}
void dual_i2s_audio_set_settings(const dsp::Settings &settings, bool persist) {
  if (!dsp::validate_settings(settings)) return;
  portENTER_CRITICAL(&g_state_lock);
  g_settings_target = settings;
  ++g_settings_generation;
  if (persist) {
    g_settings_dirty = true;
    g_settings_changed_ms = now_ms();
  }
  portEXIT_CRITICAL(&g_state_lock);
}
dsp::Settings dual_i2s_audio_settings() {
  portENTER_CRITICAL(&g_state_lock);
  const dsp::Settings settings = g_settings_target;
  portEXIT_CRITICAL(&g_state_lock);
  return settings;
}
void dual_i2s_audio_set_channel_mode(dsp::ChannelMode mode) {
  portENTER_CRITICAL(&g_state_lock);
  g_channel_mode = mode;
  ++g_settings_generation;
  portEXIT_CRITICAL(&g_state_lock);
}
dsp::ChannelMode dual_i2s_audio_channel_mode() {
  portENTER_CRITICAL(&g_state_lock);
  const dsp::ChannelMode mode = g_channel_mode;
  portEXIT_CRITICAL(&g_state_lock);
  return mode;
}
float dual_i2s_expression_level() {
  const uint32_t now = now_ms();
  portENTER_CRITICAL(&g_state_lock);
  const float level = now - g_expression_updated_ms < 150 ? g_expression_level : 0;
  portEXIT_CRITICAL(&g_state_lock);
  return level;
}
dsp::Diagnostics dual_i2s_dsp_diagnostics() {
  portENTER_CRITICAL(&g_state_lock);
  const dsp::Diagnostics result = g_dsp_snapshot;
  portEXIT_CRITICAL(&g_state_lock);
  return result;
}
DualI2sDiagnostics dual_i2s_diagnostics() {
  DualI2sDiagnostics result = g_diagnostics;
  result.completed_frames_left =
      __atomic_load_n(&g_left_isr.completed_frames, __ATOMIC_RELAXED);
  result.completed_frames_right =
      __atomic_load_n(&g_right_isr.completed_frames, __ATOMIC_RELAXED);
  result.send_queue_overflows_left =
      __atomic_load_n(&g_left_isr.queue_overflows, __ATOMIC_RELAXED);
  result.send_queue_overflows_right =
      __atomic_load_n(&g_right_isr.queue_overflows, __ATOMIC_RELAXED);
  result.completion_counter_delta =
      result.completed_frames_left >= result.completed_frames_right
          ? result.completed_frames_left - result.completed_frames_right
          : result.completed_frames_right - result.completed_frames_left;
  // Logical synchronization means both physical outputs accepted every block
  // generated from the same PCM frame. Independent TX EOF callback totals can
  // be offset and drift slightly, so they remain diagnostic-only.
  if (result.submitted_frames_left != result.submitted_frames_right ||
      result.fault_muted)
    result.synchronized = false;
  return result;
}

void dual_i2s_audio_tick() {
  bool commit = false;
  dsp::Settings settings;
  portENTER_CRITICAL(&g_state_lock);
  if (g_settings_dirty && now_ms() - g_settings_changed_ms >=
                              kSettingsCommitDelayMs) {
    commit = true;
    settings = g_settings_target;
    g_settings_dirty = false;
  }
  portEXIT_CRITICAL(&g_state_lock);
  if (!commit) return;
  nvs_handle_t nvs = 0;
  esp_err_t err = nvs_open("spkr_dsp", NVS_READWRITE, &nvs);
  if (err == ESP_OK) {
    const PersistedSettings stored{kSettingsVersion, settings};
    err = nvs_set_blob(nvs, "settings", &stored, sizeof(stored));
    if (err == ESP_OK) err = nvs_commit(nvs);
    nvs_close(nvs);
  }
  if (err != ESP_OK)
    ESP_LOGE(kTag, "deferred DSP settings commit failed: %s",
             esp_err_to_name(err));
}

void dual_i2s_audio_emergency_mute() {
  portENTER_CRITICAL(&g_state_lock);
  g_mute_target = true;
  ++g_settings_generation;
  portEXIT_CRITICAL(&g_state_lock);
  g_diagnostics.fault_muted = true;
  g_recovery_pending = true;
}

bool dual_i2s_audio_set_output_enabled(bool enabled, uint32_t timeout_ms) {
  if (g_output_task == nullptr || g_output_control_done == nullptr) return false;
  if (!enabled) {
    (void)dual_i2s_audio_fade_to_mute(std::min<uint32_t>(timeout_ms, 100));
    drain_ready_queue();
  }
  if (g_channels_enabled == enabled && !g_output_control_pending) return true;
  g_output_target_enabled = enabled;
  g_output_control_succeeded = false;
  g_output_control_pending = true;
  if (xSemaphoreTake(g_output_control_done, pdMS_TO_TICKS(timeout_ms)) != pdTRUE)
    return false;
  return g_output_control_succeeded && g_channels_enabled == enabled &&
         g_diagnostics.output_enabled == enabled;
}

bool dual_i2s_audio_set_right_pin_isolation(bool bclk_forced_low,
                                            bool ws_forced_low,
                                            bool dout_forced_low) {
  if (g_channels_enabled || g_output_control_pending) return false;
  __atomic_store_n(&g_right_bclk_forced_low, bclk_forced_low,
                   __ATOMIC_RELAXED);
  __atomic_store_n(&g_right_ws_forced_low, ws_forced_low, __ATOMIC_RELAXED);
  __atomic_store_n(&g_right_dout_forced_low, dout_forced_low,
                   __ATOMIC_RELAXED);
  g_diagnostics.right_bclk_forced_low = bclk_forced_low;
  g_diagnostics.right_ws_forced_low = ws_forced_low;
  g_diagnostics.right_dout_forced_low = dout_forced_low;
  if (bclk_forced_low)
    force_gpio_low(CONFIG_LUNAREDU_SMART_SPEAKER_RIGHT_I2S_BCLK);
  if (ws_forced_low)
    force_gpio_low(CONFIG_LUNAREDU_SMART_SPEAKER_RIGHT_I2S_WS);
  if (dout_forced_low)
    force_gpio_low(CONFIG_LUNAREDU_SMART_SPEAKER_RIGHT_I2S_DOUT);
  ESP_LOGW(kTag, "right I2S isolation BCLK=%d WS=%d DIN=%d",
           bclk_forced_low, ws_forced_low, dout_forced_low);
  return true;
}

} // namespace smart_speaker
