#include "configuration/settings.h"
#include "kernel/managed_task.h"
#include "lunaros_audio_dual_i2s.h"
#include "dual_i2s_internal.h"

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
#include <soc/gpio_sig_map.h>

#include "sdkconfig.h"
#include "dual_i2s_sync.h"
#include "lunaros_audio_pcm_clock.h"
#include "realtime/gmf_pcm_pipeline.h"

namespace lunaredu::audio {
namespace audio_internal {

DualI2sConfig g_config;
constexpr const char *kTag = "audio.dual_i2s";
constexpr size_t kFramesPerBlock = kI2sOutputBlockFrames;
// Keep the DMA ring large enough to cover the output task's scheduling
// jitter, but leave a contiguous internal-DMA block for SDMMC sector reads.
// Two TX-only I2S channels drive the independent amplifiers.
constexpr size_t kDmaDescriptorCount = 4;

// i2s_channel_write takes milliseconds, not FreeRTOS ticks.
constexpr uint32_t kWriteTimeout = 12;

constexpr uint32_t kSettingsCommitDelayMs = 1500;
constexpr uint32_t kSettingsVersion = 1;

struct AudioBlock {
  uint32_t track_generation = 0;
  uint32_t sequence = 0;
  uint16_t frames = 0;
  bool bypass_volume = false;
  uint16_t external_gain_q15 = 32767;
  int16_t left[kFramesPerBlock]{};
  int16_t right[kFramesPerBlock]{};
};

struct PersistedSettings {
  uint32_t version = kSettingsVersion;
  dsp::Settings settings{};
  uint32_t reserved_v1=0; // Retired spectrum flag and padding in the persisted V1 layout.
};
static_assert(sizeof(dsp::Settings)==36 && sizeof(PersistedSettings)==44);

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
std::atomic<bool> g_draining{false};
uint32_t g_pending_sample_rate = 44100;
uint32_t g_sequence = 0;
int32_t g_left_bus[kFramesPerBlock * 2]{};
int32_t g_right_bus[kFramesPerBlock * 2]{};
int32_t g_zero_bus[kFramesPerBlock * 2]{};
dsp::AudioProcessor g_processor;
GmfPcmPipeline g_pipeline;
AudioBlock *g_processing_block = nullptr;
std::array<int16_t,kFramesPerBlock*2> g_interleaved{};
uint32_t g_last_write_started_ms = 0;
portMUX_TYPE g_state_lock = portMUX_INITIALIZER_UNLOCKED;
PcmClock g_track_clock;
uint32_t g_track_clock_generation = 0;
dsp::Settings g_settings_target{};
dsp::ChannelMode g_channel_mode = dsp::ChannelMode::kStereo;
int g_volume_target = 50;
uint32_t g_settings_generation = 1;
bool g_settings_dirty = false;
uint32_t g_settings_changed_ms = 0;
bool g_mute_target = true;
volatile bool g_output_muted = true;
dsp::Diagnostics g_dsp_snapshot{};
DualI2sDiagnostics g_diagnostics{};
IsrCounters g_left_isr{};
IsrCounters g_right_isr{};

uint32_t now_ms() {
  return static_cast<uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

uint32_t pcm_write_timeout_ms(size_t frames) {
  const uint32_t rate=std::clamp(g_pending_sample_rate,uint32_t(8000),uint32_t(48000));
  const uint32_t block_ms=uint32_t((frames*1000+rate-1)/rate);
  // IDF floors milliseconds to ticks. Include one tick of phase uncertainty,
  // and enough time for a full DMA block at the supported low sample rates.
  const uint32_t duration=std::max(kWriteTimeout,block_ms+uint32_t(portTICK_PERIOD_MS));
  return ((duration+portTICK_PERIOD_MS-1)/portTICK_PERIOD_MS)*portTICK_PERIOD_MS;
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

esp_err_t create_first_tx_channel() {
  i2s_chan_config_t channel =
      I2S_CHANNEL_DEFAULT_CONFIG(g_config.left.port, I2S_ROLE_MASTER);
  channel.dma_desc_num = kDmaDescriptorCount;
  channel.dma_frame_num = kFramesPerBlock;
  channel.auto_clear = true;
  ESP_RETURN_ON_ERROR(i2s_new_channel(&channel, &g_left_tx, nullptr), kTag,
                      "new I2S0 TX channel");

  i2s_std_config_t tx_standard{};
  tx_standard.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(44100);
  tx_standard.clk_cfg.clk_src = I2S_CLK_SRC_DEFAULT;
  tx_standard.slot_cfg = make_slot_config();
  tx_standard.gpio_cfg = make_gpio_config(
      g_config.left.bclk,
      g_config.left.ws,
      g_config.left.dout);
  ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(g_left_tx, &tx_standard), kTag,
                      "init I2S0 TX");

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
    if(!consume_output_control(index))(void)xQueueSend(g_free_queue, &index, 0);
}

esp_err_t stop_channels() {
  esp_err_t result = ESP_OK;
  if (g_channels_enabled) {
    const esp_err_t left = i2s_channel_disable(g_left_tx);
    const esp_err_t right = g_right_tx?i2s_channel_disable(g_right_tx):ESP_OK;
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
  if(g_right_tx)ESP_RETURN_ON_ERROR(i2s_channel_reconfig_std_clock(g_right_tx, &clock), kTag,
                      "right clock");
  ESP_RETURN_ON_ERROR(i2s_channel_reconfig_std_slot(g_left_tx, &slot), kTag,
                      "left slot");
  if(g_right_tx)ESP_RETURN_ON_ERROR(i2s_channel_reconfig_std_slot(g_right_tx, &slot), kTag,
                      "right slot");
  const i2s_std_gpio_config_t left_gpio = make_gpio_config(
      g_config.left.bclk,
      g_config.left.ws,
      g_config.left.dout);
  const bool right_bclk_forced_low =
      __atomic_load_n(&g_right_bclk_forced_low, __ATOMIC_RELAXED);
  const bool right_ws_forced_low =
      __atomic_load_n(&g_right_ws_forced_low, __ATOMIC_RELAXED);
  const bool right_dout_forced_low =
      __atomic_load_n(&g_right_dout_forced_low, __ATOMIC_RELAXED);
  const i2s_std_gpio_config_t right_gpio = make_gpio_config(
      right_bclk_forced_low
          ? GPIO_NUM_NC
          : g_config.right.bclk,
      right_ws_forced_low ? GPIO_NUM_NC
                          : g_config.right.ws,
      right_dout_forced_low
          ? GPIO_NUM_NC
          : g_config.right.dout);
  ESP_RETURN_ON_ERROR(i2s_channel_reconfig_std_gpio(g_left_tx, &left_gpio),
                      kTag, "left GPIO");
  if(g_right_tx)ESP_RETURN_ON_ERROR(i2s_channel_reconfig_std_gpio(g_right_tx, &right_gpio),
                      kTag, "right GPIO");
  if (g_right_tx&&right_bclk_forced_low)
    force_gpio_low(g_config.right.bclk);
  if (g_right_tx&&right_ws_forced_low)
    force_gpio_low(g_config.right.ws);
  if (g_right_tx&&right_dout_forced_low)
    force_gpio_low(g_config.right.dout);
  size_t left_loaded = 0;
  size_t right_loaded = 0;
  ESP_RETURN_ON_ERROR(i2s_channel_preload_data(
                          g_left_tx, g_zero_bus, sizeof(g_zero_bus), &left_loaded),
                      kTag, "left zero preload");
  if(g_right_tx)ESP_RETURN_ON_ERROR(i2s_channel_preload_data(g_right_tx, g_zero_bus,
                                                sizeof(g_zero_bus),
                                                &right_loaded),
                      kTag, "right zero preload");
  if (g_right_tx&&left_loaded != right_loaded) return ESP_ERR_INVALID_SIZE;
  ESP_RETURN_ON_ERROR(i2s_channel_enable(g_left_tx), kTag, "left enable");
  const esp_err_t right_enable = g_right_tx?i2s_channel_enable(g_right_tx):ESP_OK;
  if (right_enable != ESP_OK) {
    (void)i2s_channel_disable(g_left_tx);
    return right_enable;
  }

  i2s_chan_info_t left_info{};
  i2s_chan_info_t right_info{};
  const esp_err_t left_info_err = i2s_channel_get_info(g_left_tx, &left_info);
  const esp_err_t right_info_err =
      g_right_tx?i2s_channel_get_info(g_right_tx, &right_info):ESP_OK;
  if (left_info_err != ESP_OK || right_info_err != ESP_OK) {
    if(g_right_tx)(void)i2s_channel_disable(g_right_tx);
    (void)i2s_channel_disable(g_left_tx);
    return left_info_err != ESP_OK ? left_info_err : right_info_err;
  }
  g_diagnostics.left_bclk_hz = left_info.bclk_hz;
  g_diagnostics.right_bclk_hz = right_info.bclk_hz;
  g_diagnostics.left_mclk_hz = left_info.mclk_hz;
  g_diagnostics.right_mclk_hz = right_info.mclk_hz;
  g_diagnostics.left_clock_source = static_cast<int>(left_info.clk_src);
  g_diagnostics.right_clock_source = static_cast<int>(right_info.clk_src);
  g_diagnostics.clocks_match = !g_right_tx||(
      left_info.bclk_hz == right_info.bclk_hz &&
      left_info.mclk_hz == right_info.mclk_hz &&
      left_info.clk_src == right_info.clk_src);
  if (!g_diagnostics.clocks_match) {
    if(g_right_tx)(void)i2s_channel_disable(g_right_tx);
    (void)i2s_channel_disable(g_left_tx);
    return ESP_ERR_INVALID_STATE;
  }
  g_channels_enabled = true;
  g_diagnostics.output_count=g_config.single_output?1:2;
  g_diagnostics.output_enabled = true;
  g_diagnostics.right_bclk_forced_low = right_bclk_forced_low;
  g_diagnostics.right_ws_forced_low = right_ws_forced_low;
  g_diagnostics.right_dout_forced_low = right_dout_forced_low;
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
      g_processor.request_mute(g_config.fade_ms);
    }
    else g_processor.request_unmute(g_config.fade_ms);
    seen_generation = generation;
  }
}

bool render_stereo(void *,const int16_t *input,int16_t *output,size_t frames) {
  const auto &block=*g_processing_block;
  return g_processor.process_pcm(input,output,frames,2,!block.bypass_volume,block.external_gain_q15/32767.0F);
}

GmfPcmPipeline::Result write_stereo(void *,const int16_t *pcm,size_t frames) {
  const auto &block=*g_processing_block;
  portENTER_CRITICAL(&g_state_lock);
  const bool current=block.track_generation==g_track_clock_generation;
  portEXIT_CRITICAL(&g_state_lock);
  if(!current||g_output_stop.load())return GmfPcmPipeline::Result::Cancelled;
  for(size_t i=0;i<frames;++i){
    const int32_t left=static_cast<int32_t>(uint32_t(uint16_t(pcm[i*2]))<<16);
    const int32_t right=static_cast<int32_t>(uint32_t(uint16_t(pcm[i*2+1]))<<16);
    // Existing physical mapping: I2S0 -> right speaker, I2S1 -> left.
    g_left_bus[i*2]=g_left_bus[i*2+1]=right;
    g_right_bus[i*2]=g_right_bus[i*2+1]=left;
  }
  const size_t wanted=frames*sizeof(int32_t)*2;
  size_t left_written=0,right_written=0;
  const uint32_t started=now_ms();
  if(g_last_write_started_ms)
    g_diagnostics.maximum_write_gap_ms=std::max(g_diagnostics.maximum_write_gap_ms,started-g_last_write_started_ms);
  g_last_write_started_ms=started;
  const auto timeout_ms=pcm_write_timeout_ms(frames);
  const auto left_err=i2s_channel_write(g_left_tx,g_left_bus,wanted,&left_written,timeout_ms);
  const auto right_err=g_right_tx?i2s_channel_write(g_right_tx,g_right_bus,wanted,&right_written,timeout_ms):ESP_OK;
  g_diagnostics.maximum_write_time_ms=std::max(g_diagnostics.maximum_write_time_ms,now_ms()-started);
  if(left_err==ESP_ERR_TIMEOUT)++g_diagnostics.timeouts_left;
  if(right_err==ESP_ERR_TIMEOUT)++g_diagnostics.timeouts_right;
  if(left_written!=wanted)++g_diagnostics.short_writes_left;
  if(g_right_tx&&right_written!=wanted)++g_diagnostics.short_writes_right;
  if(left_err!=ESP_OK||right_err!=ESP_OK||left_written!=wanted||(g_right_tx&&right_written!=wanted)){
    mark_fault("short write or timeout");return GmfPcmPipeline::Result::Failed;
  }
  portENTER_CRITICAL(&g_state_lock);
  if(block.track_generation==g_track_clock_generation)
    g_track_clock.advance(frames,g_diagnostics.sample_rate);
  portEXIT_CRITICAL(&g_state_lock);
  g_diagnostics.submitted_frames_left+=frames;if(g_right_tx)g_diagnostics.submitted_frames_right+=frames;
  return GmfPcmPipeline::Result::Written;
}

bool close_audio_pipeline() { return g_pipeline.close(); }

void do_recovery_or_reconfigure() {
  const bool was_reconfigure = g_reconfigure_pending;
  const uint32_t rate = g_pending_sample_rate;
  g_processor.request_mute(1);
  __atomic_store_n(&g_audio_expected, false, __ATOMIC_RELAXED);
  drain_ready_queue();
  (void)stop_channels();
  const esp_err_t err = g_pipeline.close()?configure_and_start_channels(rate):ESP_FAIL;
  if (err == ESP_OK && g_processor.configure(rate, dual_i2s_audio_settings()) &&
      g_pipeline.open(rate,render_stereo,write_stereo,nullptr,2)) {
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
      g_processor.request_unmute(g_config.fade_ms);
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
  uint32_t last_silence_ms = last_block_ms;
  uint32_t silent_sample_remainder = 0;
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
    const auto previous_generation=seen_generation;
    apply_control_targets(seen_generation);
    if(previous_generation!=seen_generation){last_silence_ms=now_ms();silent_sample_remainder=0;}
    if (g_output_control_pending) {
      const bool enable = g_output_target_enabled;
      esp_err_t err = ESP_OK;
      if (enable) {
        err = configure_and_start_channels(g_pending_sample_rate);
      } else {
        err = stop_channels();
        dual_i2s_quiet_audio_pins(g_config);
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
                                         now_ms(), completion_monitor_active,!g_config.single_output)) {
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
    // Active monitoring/fades keep their cadence; a fully muted output sleeps
    // until PCM or an explicit control request arrives. Never busy-spin at low HZ.
    const TickType_t wait=g_processor.muted()?portMAX_DELAY:std::max<TickType_t>(1,pdMS_TO_TICKS(2));
    if (xQueueReceive(g_ready_queue, &index, wait) != pdTRUE) {
      const uint32_t silence_now=now_ms();
      if (g_processor.fade_state() == dsp::FadeState::kRampDown) {
        // With no PCM, advance the envelope by elapsed wall time. A fixed 128
        // samples per tick stretches a 30 ms fade beyond the retirement window
        // at 44.1/48 kHz with a 100 Hz tick, repeatedly restarting focus release.
        const uint32_t elapsed=std::min(silence_now-last_silence_ms,uint32_t(100));
        const uint64_t budget=uint64_t(g_diagnostics.sample_rate)*elapsed+silent_sample_remainder;
        const size_t frames=budget/1000;
        silent_sample_remainder=budget%1000;
        for (size_t i = 0; i < frames; ++i) {
          float left = 0.0f;
          float right = 0.0f;
          g_processor.finalize_output(left, right);
        }
      }
      last_silence_ms=silence_now;
      if (g_processor.muted()) {
        stream_active = false;
        __atomic_store_n(&g_audio_expected, false, __ATOMIC_RELAXED);
      }
      g_output_muted = g_processor.muted();
      if (stream_active &&!g_draining.load()&&
          __atomic_load_n(&g_audio_expected, __ATOMIC_RELAXED) &&
          !g_processor.muted() &&
          now_ms() - last_block_ms > 20) {
        ++g_diagnostics.inferred_underruns;
        mark_fault("PCM queue underrun");
      }
      continue;
    }
    if(consume_output_control(index))continue;
    AudioBlock &block = g_blocks[index];
    portENTER_CRITICAL(&g_state_lock);
    const bool current=block.track_generation==g_track_clock_generation;
    portEXIT_CRITICAL(&g_state_lock);
    if(!current){(void)xQueueSend(g_free_queue,&index,0);continue;}
    stream_active = true;
    bool unmuted_target = false;
    portENTER_CRITICAL(&g_state_lock);
    unmuted_target = !g_mute_target;
    portEXIT_CRITICAL(&g_state_lock);
    // Blocks already queued when pause/stop requests mute must not turn DMA
    // starvation monitoring back on. Only a block belonging to an unmuted
    // playback target proves that continuous PCM is expected.
    if (unmuted_target&&!g_draining.load())
      __atomic_store_n(&g_audio_expected, true, __ATOMIC_RELAXED);
    last_block_ms = now_ms();
    for(size_t i=0;i<block.frames;++i){
      g_interleaved[i*2]=block.left[i];g_interleaved[i*2+1]=block.right[i];
    }
    g_processing_block=&block;
    const auto result=g_pipeline.write(g_interleaved.data(),block.frames);
    last_silence_ms=now_ms();silent_sample_remainder=0;
    g_processing_block=nullptr;
    g_output_muted=g_processor.muted();
    portENTER_CRITICAL(&g_state_lock);
    g_dsp_snapshot=g_processor.diagnostics();
    portEXIT_CRITICAL(&g_state_lock);
    if(result==GmfPcmPipeline::Result::Failed&&!g_recovery_pending)
      mark_fault("GMF render failed");
    (void)xQueueSend(g_free_queue, &index, 0);
  }
}

void load_settings() {
  g_settings_target=dsp::preset_settings(dsp::Preset::kFlat);
  PersistedSettings stored{};size_t size=0;std::string error;
  if(lunaredu::lunaros::settings_read_blob(g_config.settings_namespace,"settings",&stored,sizeof(stored),size,error)==lunaredu::lunaros::SettingsReadResult::Found &&
     size==sizeof(stored) && stored.version==kSettingsVersion && dsp::validate_settings(stored.settings))g_settings_target=stored.settings;
}

bool prepare_audio_pipeline() {
  load_settings();
  if (create_first_tx_channel() != ESP_OK ||
      (!g_config.single_output&&create_channel(g_config.right.port, g_config.right.bclk,
                     g_config.right.ws,
                     g_config.right.dout, &g_right_isr,
                     &g_right_tx) != ESP_OK))
    return false;
  if (!g_processor.configure(44100, g_settings_target)) return false;
  if (!g_pipeline.open(44100,render_stereo,write_stereo,nullptr,2)) return false;
  g_processor.set_volume(g_volume_target);
  if (configure_and_start_channels(44100) != ESP_OK) return false;
  return true;
}
void reset_audio_control() {
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
  g_last_write_started_ms=0;
  g_draining=false;
}
} // namespace audio_internal
using namespace audio_internal;

bool valid_dual_i2s_config(const DualI2sConfig &config) {
  if(config.left.port<0||config.left.port>1||!config.settings_namespace||!config.task_id||!config.service_id)return false;
  if(!config.single_output&&(config.right.port<0||config.right.port>1||config.left.port==config.right.port))return false;
  const int pins[]={config.left.ws,config.left.bclk,config.left.dout,config.right.ws,config.right.bclk,config.right.dout};
  for(size_t i=0;i<(config.single_output?3:std::size(pins));++i){
    if(!GPIO_IS_VALID_OUTPUT_GPIO(pins[i]))return false;
    for(size_t j=0;j<i;++j)if(pins[i]==pins[j])return false;
  }
  return true;
}
bool dual_i2s_audio_drain(uint32_t timeout_ms) {
  g_draining=true;
  __atomic_store_n(&g_audio_expected,false,__ATOMIC_RELAXED);
  const auto started=now_ms();
  while(g_free_queue&&uxQueueMessagesWaiting(g_free_queue)!=kBlockCount){
    if(now_ms()-started>=timeout_ms)return false;
    vTaskDelay(pdMS_TO_TICKS(2));
  }
  // Both DMA rings must finish after the last software block was submitted.
  // No producer may submit PCM while its drain callback is executing.
  const uint32_t rate=std::clamp(g_pending_sample_rate,uint32_t(8000),uint32_t(48000));
  const uint32_t dma_ms=uint32_t((kFramesPerBlock*kDmaDescriptorCount*1000+rate-1)/rate)+4;
  if(now_ms()-started+dma_ms>timeout_ms)return false;
  vTaskDelay(pdMS_TO_TICKS(dma_ms+portTICK_PERIOD_MS-1));
  return true;
}
bool dual_i2s_audio_discard(uint32_t timeout_ms) {
  if(!dual_i2s_audio_fade_to_mute(timeout_ms))return false;
  return dual_i2s_audio_drain(timeout_ms);
}
void dual_i2s_quiet_audio_pins(const DualI2sConfig &pins) {
  if(!valid_dual_i2s_config(pins))return;
  uint64_t mask = (1ULL << pins.left.ws) |
                        (1ULL << pins.left.bclk) |
                        (1ULL << pins.left.dout);
  if(!pins.single_output)mask|=(1ULL << pins.right.ws) |
                        (1ULL << pins.right.bclk) |
                        (1ULL << pins.right.dout);
  gpio_config_t config{};
  config.pin_bit_mask = mask;
  config.mode = GPIO_MODE_OUTPUT;
  config.pull_up_en = GPIO_PULLUP_DISABLE;
  config.pull_down_en = GPIO_PULLDOWN_ENABLE;
  config.intr_type = GPIO_INTR_DISABLE;
  (void)gpio_config(&config);
  const int outputs[]={pins.left.ws,pins.left.bclk,pins.left.dout,pins.right.ws,pins.right.bclk,pins.right.dout};
  for(size_t i=0;i<(pins.single_output?3U:6U);++i){
    const int gpio=outputs[i];
    esp_rom_gpio_connect_out_signal(static_cast<uint32_t>(gpio),
                                    SIG_GPIO_OUT_IDX, false, false);
    (void)gpio_set_level(static_cast<gpio_num_t>(gpio), 0);
  }
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

esp_err_t write_audio_blocks(const void *audio_buffer, size_t len,
                             size_t *bytes_written, uint32_t timeout_ms,
                             bool bypass_volume,
                             uint16_t external_gain_q15 = 32767) {
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
    const TickType_t wait = pdMS_TO_TICKS(std::min(timeout_ms,pcm_write_timeout_ms(kFramesPerBlock)));
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
    block.external_gain_q15 = external_gain_q15;
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

esp_err_t dual_i2s_audio_write(const void *audio_buffer, size_t len,
                               size_t *bytes_written, uint32_t timeout_ms,
                               void *) {
  g_draining=false;
  return write_audio_blocks(audio_buffer, len, bytes_written, timeout_ms,
                            false);
}

esp_err_t dual_i2s_audio_write_with_gain(const int16_t *pcm,size_t bytes,
    size_t *written,uint16_t gain,uint32_t timeout) {
  if(gain>32767)return ESP_ERR_INVALID_ARG;
  g_draining=false;
  return write_audio_blocks(pcm,bytes,written,timeout,true,gain);
}

esp_err_t dual_i2s_audio_reconfigure(uint32_t rate, uint32_t bits,
                                     unsigned channels) {
  if (!g_reconfigure_done || g_output_stop.load()) return ESP_ERR_INVALID_STATE;
  if (rate < 8000 || rate > 48000 || bits != 16 ||
      channels != 2) {
    dual_i2s_audio_emergency_mute();
    return ESP_ERR_INVALID_ARG;
  }
  (void)dual_i2s_audio_fade_to_mute(100);
  g_pending_sample_rate = rate;
  g_reconfigure_pending = true;
  wake_output_control();
  if (xSemaphoreTake(g_reconfigure_done, pdMS_TO_TICKS(250)) != pdTRUE)
    return ESP_ERR_TIMEOUT;
  if (!g_diagnostics.synchronized) return ESP_FAIL;
  dual_i2s_audio_fade_up();
  return ESP_OK;
}

esp_err_t dual_i2s_audio_mute(AudioMute setting) {
  if (setting == AudioMute::Mute)
    __atomic_store_n(&g_audio_expected, false, __ATOMIC_RELAXED);
  portENTER_CRITICAL(&g_state_lock);
  g_mute_target = setting == AudioMute::Mute;
  ++g_settings_generation;
  portEXIT_CRITICAL(&g_state_lock);
  wake_output_control();
  return ESP_OK;
}

bool dual_i2s_audio_fade_to_mute(uint32_t timeout_ms) {
  (void)dual_i2s_audio_mute(AudioMute::Mute);
  const uint32_t start = now_ms();
  while (!g_output_muted && now_ms() - start < timeout_ms)
    vTaskDelay(pdMS_TO_TICKS(2));
  return g_output_muted;
}
void dual_i2s_audio_fade_up() {
  (void)dual_i2s_audio_mute(AudioMute::Unmute);
}

void dual_i2s_audio_set_volume(int volume) {
  portENTER_CRITICAL(&g_state_lock);
  g_volume_target = std::clamp(volume, 0, dsp::kSystemMaxVolume);
  ++g_settings_generation;
  portEXIT_CRITICAL(&g_state_lock);
  wake_output_control();
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
  wake_output_control();
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
  wake_output_control();
}
dsp::ChannelMode dual_i2s_audio_channel_mode() {
  portENTER_CRITICAL(&g_state_lock);
  const dsp::ChannelMode mode = g_channel_mode;
  portEXIT_CRITICAL(&g_state_lock);
  return mode;
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
  result.completion_counter_delta = g_config.single_output?0:
      result.completed_frames_left >= result.completed_frames_right
          ? result.completed_frames_left - result.completed_frames_right
          : result.completed_frames_right - result.completed_frames_left;
  // Logical synchronization means both physical outputs accepted every block
  // generated from the same PCM frame. Independent TX EOF callback totals can
  // be offset and drift slightly, so they remain diagnostic-only.
  if ((!g_config.single_output&&result.submitted_frames_left != result.submitted_frames_right) ||
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
  const PersistedSettings stored{kSettingsVersion,settings};
  const auto entry=lunaredu::lunaros::SettingsWrite::blob("settings",&stored,sizeof(stored));
  std::string error;
  if(!lunaredu::lunaros::settings_write_batch(g_config.settings_namespace,&entry,1,error))
    ESP_LOGE(kTag,"deferred DSP settings commit failed: %s",error.c_str());
}

void dual_i2s_audio_emergency_mute() {
  portENTER_CRITICAL(&g_state_lock);
  g_mute_target = true;
  ++g_settings_generation;
  portEXIT_CRITICAL(&g_state_lock);
  g_diagnostics.fault_muted = true;
  g_recovery_pending = true;
  wake_output_control();
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
  wake_output_control();
  if (xSemaphoreTake(g_output_control_done, pdMS_TO_TICKS(timeout_ms)) != pdTRUE)
    return false;
  return g_output_control_succeeded && g_channels_enabled == enabled &&
         g_diagnostics.output_enabled == enabled;
}

bool dual_i2s_audio_set_right_pin_isolation(bool bclk_forced_low,
                                            bool ws_forced_low,
                                            bool dout_forced_low) {
  if (g_config.single_output||g_channels_enabled || g_output_control_pending) return false;
  __atomic_store_n(&g_right_bclk_forced_low, bclk_forced_low,
                   __ATOMIC_RELAXED);
  __atomic_store_n(&g_right_ws_forced_low, ws_forced_low, __ATOMIC_RELAXED);
  __atomic_store_n(&g_right_dout_forced_low, dout_forced_low,
                   __ATOMIC_RELAXED);
  g_diagnostics.right_bclk_forced_low = bclk_forced_low;
  g_diagnostics.right_ws_forced_low = ws_forced_low;
  g_diagnostics.right_dout_forced_low = dout_forced_low;
  if (bclk_forced_low)
    force_gpio_low(g_config.right.bclk);
  if (ws_forced_low)
    force_gpio_low(g_config.right.ws);
  if (dout_forced_low)
    force_gpio_low(g_config.right.dout);
  ESP_LOGW(kTag, "right I2S isolation BCLK=%d WS=%d DIN=%d",
           bclk_forced_low, ws_forced_low, dout_forced_low);
  return true;
}

} // namespace lunaredu::audio
