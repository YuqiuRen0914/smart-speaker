#pragma once

#include <cstddef>
#include <cstdint>

#include "lunaros_audio_file_player.h"
#include <esp_err.h>

#include "lunaros_audio_dsp.h"

namespace lunaredu::lunaros { class KernelRuntime; }

namespace lunaredu::audio {
inline constexpr size_t kI2sOutputBlockFrames=128;
inline constexpr size_t kI2sOutputBlockCount=8;

struct DualI2sDiagnostics {
  uint8_t output_count = 2;
  uint64_t submitted_frames_left = 0;
  uint64_t submitted_frames_right = 0;
  uint64_t completed_frames_left = 0;
  uint64_t completed_frames_right = 0;
  uint64_t completion_counter_delta = 0;
  uint32_t completion_stalls_left = 0;
  uint32_t completion_stalls_right = 0;
  uint32_t short_writes_left = 0;
  uint32_t short_writes_right = 0;
  uint32_t timeouts_left = 0;
  uint32_t timeouts_right = 0;
  uint32_t send_queue_overflows_left = 0;
  uint32_t send_queue_overflows_right = 0;
  uint32_t inferred_underruns = 0;
  uint32_t input_queue_overflows = 0;
  uint32_t resync_count = 0;
  uint32_t invalid_pcm_blocks = 0;
  uint32_t maximum_write_gap_ms = 0;
  uint32_t maximum_write_time_ms = 0;
  uint32_t sample_rate = 44100;
  uint32_t left_bclk_hz = 0;
  uint32_t right_bclk_hz = 0;
  uint32_t left_mclk_hz = 0;
  uint32_t right_mclk_hz = 0;
  int left_clock_source = 0;
  int right_clock_source = 0;
  bool synchronized = false;
  bool fault_muted = false;
  bool clocks_match = false;
  bool output_enabled = false;
  bool right_bclk_forced_low = false;
  bool right_ws_forced_low = false;
  bool right_dout_forced_low = false;
};

struct DualI2sPins { int port=0,bclk=-1,ws=-1,dout=-1; };
struct DualI2sConfig {
  // Names and settings namespace must outlive the service.
  DualI2sPins left,right;
  uint32_t fade_ms=30;
  const char *settings_namespace="audio_dsp";
  const char *task_id="audio.dual_i2s";
  const char *service_id="audio";
  // A single wired TX reuses the same GMF/DSP task and fixed PCM pool.
  // No second controller, DMA or GPIO is allocated; right is ignored.
  bool single_output=false;
};
bool valid_dual_i2s_config(const DualI2sConfig &);
bool setup_dual_i2s_audio(lunaredu::lunaros::KernelRuntime &runtime,const DualI2sConfig &);
bool dual_i2s_audio_drain(uint32_t timeout_ms);
bool dual_i2s_audio_discard(uint32_t timeout_ms);
void dual_i2s_quiet_audio_pins(const DualI2sConfig &);
// Decoder must be stopped before shutdown. Failure retains live resources.
bool stop_dual_i2s_audio(lunaredu::lunaros::KernelRuntime &runtime);
esp_err_t dual_i2s_audio_write(const void *audio_buffer, size_t len,
                               size_t *bytes_written, uint32_t timeout_ms,
                               void *context);
// PCM-bridge callers supply their already normalized gain. The same DSP,
// fades and fixed output pool apply without applying the UI volume twice.
esp_err_t dual_i2s_audio_write_with_gain(const int16_t *,size_t bytes,
    size_t *bytes_written,uint16_t gain_q15,uint32_t timeout_ms);
esp_err_t dual_i2s_audio_reconfigure(uint32_t rate, uint32_t bits,
                                     unsigned channels);
esp_err_t dual_i2s_audio_mute(AudioMute setting);
bool dual_i2s_audio_fade_to_mute(uint32_t timeout_ms = 100);
void dual_i2s_audio_fade_up();
void dual_i2s_audio_set_volume(int volume);
void dual_i2s_audio_set_settings(const dsp::Settings &settings,
                                 bool persist = true);
dsp::Settings dual_i2s_audio_settings();
void dual_i2s_audio_set_channel_mode(dsp::ChannelMode mode);
dsp::ChannelMode dual_i2s_audio_channel_mode();
dsp::Diagnostics dual_i2s_dsp_diagnostics();
void dual_i2s_track_clock_reset();
uint32_t dual_i2s_track_position_ms();
DualI2sDiagnostics dual_i2s_diagnostics();
void dual_i2s_audio_tick();
void dual_i2s_audio_emergency_mute();
bool dual_i2s_audio_set_output_enabled(bool enabled,
                                       uint32_t timeout_ms = 500);
bool dual_i2s_audio_set_right_pin_isolation(bool bclk_forced_low,
                                            bool ws_forced_low,
                                            bool dout_forced_low);

} // namespace lunaredu::audio
