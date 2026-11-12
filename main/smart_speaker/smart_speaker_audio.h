#pragma once

#include <cstddef>
#include <cstdint>

#include <audio_player.h>
#include <esp_err.h>

#include "smart_speaker_dsp.h"
#include "smart_speaker_music_spectrum.h"

namespace smart_speaker {

struct DualI2sDiagnostics {
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
  uint32_t probe_peak_i2s0 = 0;
  uint32_t probe_peak_i2s1 = 0;
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
  bool microphone_input_enabled = false;
  bool right_bclk_forced_low = false;
  bool right_ws_forced_low = false;
  bool right_dout_forced_low = false;
};

bool setup_dual_i2s_audio();
// Wait for the decoded tail before the player mutes a naturally completed MP3.
bool dual_i2s_audio_drain(uint32_t timeout_ms);
// Decoder must be stopped before shutdown. Failure retains live resources.
bool stop_dual_i2s_audio();
esp_err_t dual_i2s_audio_write(void *audio_buffer, size_t len,
                               size_t *bytes_written, uint32_t timeout_ms,
                               void *context);
esp_err_t dual_i2s_audio_reconfigure(uint32_t rate, uint32_t bits,
                                     i2s_slot_mode_t channels);
esp_err_t dual_i2s_audio_mute(AUDIO_PLAYER_MUTE_SETTING setting);
bool dual_i2s_audio_fade_to_mute(uint32_t timeout_ms = 100);
void dual_i2s_audio_fade_up();
void dual_i2s_audio_set_volume(int volume);
void dual_i2s_audio_set_settings(const dsp::Settings &settings,
                                 bool persist = true);
dsp::Settings dual_i2s_audio_settings();
void dual_i2s_audio_set_channel_mode(dsp::ChannelMode mode);
dsp::ChannelMode dual_i2s_audio_channel_mode();
dsp::Diagnostics dual_i2s_dsp_diagnostics();
float dual_i2s_expression_level();
// Display owner enables capture only for the foreground song view.
void dual_i2s_music_spectrum_enable(bool enabled);
bool dual_i2s_music_spectrum_take(music::PcmWindow &out, uint32_t now);
void dual_i2s_track_clock_reset();
uint32_t dual_i2s_track_position_ms();
DualI2sDiagnostics dual_i2s_diagnostics();
void dual_i2s_audio_tick();
// Product focus gate and authoritative hit counter; consumed by audio owner.
void dual_i2s_wood_fish_update(bool enabled, uint32_t count);
void dual_i2s_audio_emergency_mute();
bool dual_i2s_audio_set_output_enabled(bool enabled,
                                       uint32_t timeout_ms = 500);
bool dual_i2s_audio_set_right_pin_isolation(bool bclk_forced_low,
                                            bool ws_forced_low,
                                            bool dout_forced_low);
bool smart_speaker_voice_audio_enable_input(bool enabled);
bool smart_speaker_voice_audio_enable_output(bool enabled);
bool smart_speaker_voice_audio_configure_output(uint32_t sample_rate);
bool smart_speaker_voice_audio_read_input(int16_t *samples, size_t sample_count,
                                          uint32_t timeout_ms);
bool smart_speaker_voice_audio_write_output(const int16_t *samples,
                                            size_t sample_count,
                                            uint32_t timeout_ms);
int smart_speaker_voice_audio_input_sample_rate();
int smart_speaker_voice_audio_output_sample_rate();

// Called before any logging or service setup from app_main().
void smart_speaker_quiet_audio_pins_early();
bool smart_speaker_validate_and_log_gpio_ownership();

} // namespace smart_speaker
