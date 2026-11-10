#pragma once

#include <cstddef>
#include <cstdint>

namespace lunaredu::audio::dsp {

constexpr int kSystemMaxVolume = 70;

enum class Preset : uint8_t { kFlat, kVoice, kMusic, kBassReduce, kCustom };
enum class ChannelMode : uint8_t {
  kStereo,
  kLeftOnly,
  kRightOnly,
  kAlternating,
  kMono,
  kMute,
};
enum class FadeState : uint8_t { kMuted, kRampUp, kPlaying, kRampDown };

struct Settings {
  Preset preset = Preset::kFlat;
  bool high_pass_enabled = true;
  float high_pass_hz = 80.0f;
  float low_frequency_hz = 120.0f;
  float low_gain_db = 0.0f;
  float mid_frequency_hz = 1000.0f;
  float mid_q = 1.0f;
  float mid_gain_db = 0.0f;
  float high_frequency_hz = 8000.0f;
  float high_gain_db = 0.0f;
};

struct Diagnostics {
  float input_peak = 0.0f;
  float output_peak = 0.0f;
  float rms = 0.0f;
  float eq_pre_gain = 1.0f;
  float limiter_gain = 1.0f;
  float protection_gain = 1.0f;
  float maximum_limiter_reduction_db = 0.0f;
  uint64_t limiter_events = 0;
  uint64_t limiter_samples = 0;
  uint64_t soft_clip_samples = 0;
  uint64_t invalid_samples = 0;
  uint32_t continuous_limiter_ms = 0;
  bool protection_active = false;
};

struct BiquadCoefficients {
  float b0 = 1.0f;
  float b1 = 0.0f;
  float b2 = 0.0f;
  float a1 = 0.0f;
  float a2 = 0.0f;
};

Settings preset_settings(Preset preset);
const char *preset_name(Preset preset);
const char *channel_mode_name(ChannelMode mode);
bool parse_preset(const char *name, Preset &preset);
bool parse_channel_mode(const char *name, ChannelMode &mode);
bool validate_settings(const Settings &settings);
float volume_gain(int volume);
bool biquad_is_stable(const BiquadCoefficients &coefficients);

class AudioProcessor {
public:
  AudioProcessor();
  ~AudioProcessor();
  AudioProcessor(const AudioProcessor &) = delete;
  AudioProcessor &operator=(const AudioProcessor &) = delete;

  bool configure(uint32_t sample_rate, const Settings &settings);
  void set_settings(const Settings &settings);
  const Settings &settings() const { return target_; }
  uint32_t sample_rate() const { return sample_rate_; }
  float effective_high_frequency_hz() const { return effective_high_hz_; }
  void set_volume(int volume);
  int volume() const { return volume_; }
  void set_channel_mode(ChannelMode mode);
  ChannelMode channel_mode() const { return channel_mode_; }

  void request_mute(uint32_t fade_ms = 40);
  void request_unmute(uint32_t fade_ms = 40);
  FadeState fade_state() const { return fade_state_; }
  float fade_gain() const { return fade_gain_; }
  bool muted() const { return fade_state_ == FadeState::kMuted; }

  // EQ/high-pass only. Kept public for deterministic host verification.
  void process_filters(float &left, float &right);
  // Full common chain through soft clipping; fade and diagnostic channel routing
  // are intentionally deferred to the single dual-I2S output task.
  void process_common(int16_t left, int16_t right, float &output_left,
                      float &output_right, float &pre_volume_mono,
                      bool apply_volume = true,
                      float external_gain = 1.0f);
  // Bounded mono/stereo PCM processing. Stable filter parameters use official
  // block kernels; transitions retain the scalar smoothing and fault behavior.
  bool process_pcm(const int16_t *,int16_t *,size_t frames,unsigned channels,
                   bool apply_volume=true,float external_gain=1.0f);
  void process_linked_limiter(float &left, float &right);
  void finalize_output(float &left, float &right);
  int16_t quantize(float sample) const;

  void reset();
  Diagnostics diagnostics() const;
  BiquadCoefficients high_pass_coefficients() const;
  BiquadCoefficients low_coefficients() const;
  BiquadCoefficients mid_coefficients() const;
  BiquadCoefficients high_coefficients() const;
  uint32_t coefficient_rebuild_count() const {
    return coefficient_rebuild_count_;
  }

private:
  struct Biquad;
  struct StereoBiquad;

  void update_smoothed_parameters();
  bool parameters_pending() const;
  void process_after_filters(float &,float &,bool,float);
  void materialize_stereo_history();
  void rebuild_coefficients();
  void rebuild_runtime_constants();
  void begin_fade(FadeState state, uint32_t fade_ms);
  float process_dc(float input, int channel);
  float soft_clip(float input);
  float next_fade_gain();

  uint32_t sample_rate_ = 48000;
  Settings target_{};
  Settings current_{};
  int volume_ = 50;
  ChannelMode channel_mode_ = ChannelMode::kStereo;
  FadeState fade_state_ = FadeState::kMuted;
  float fade_gain_ = 0.0f;
  uint32_t fade_samples_total_ = 1;
  uint32_t fade_samples_elapsed_ = 0;
  float fade_start_gain_ = 0.0f;
  float fade_phase_cos_ = 1.0f;
  float fade_phase_sin_ = 0.0f;
  float fade_step_cos_ = 1.0f;
  float fade_step_sin_ = 0.0f;
  uint64_t routed_samples_ = 0;
  float effective_high_hz_ = 8000.0f;
  // While symmetric, right DC/filter history is logically the left history.
  // Materialize before the first divergent input; then retain both until reset.
  bool channels_symmetric_ = true;
  float dc_x_[2]{};
  float dc_y_[2]{};
  float dc_feedback_ = 1.0f;
  float parameter_smoothing_amount_ = 1.0f;
  float high_pass_smoothing_amount_ = 1.0f;
  float rms_alpha_ = 0.0f;
  float thermal_attack_alpha_ = 0.0f;
  float thermal_release_alpha_ = 0.0f;
  float limiter_attack_alpha_ = 0.0f;
  float limiter_release_alpha_ = 0.0f;
  float protection_attack_multiplier_ = 1.0f;
  float protection_release_multiplier_ = 1.0f;
  float protection_active_threshold_ = 1.0f;
  float eq_pre_gain_ = 1.0f;
  float volume_gain_ = 0.25f;
  float limiter_gain_ = 1.0f;
  float minimum_limiter_gain_ = 1.0f;
  float rms_power_ = 0.0f;
  float thermal_power_ = 0.0f;
  float protection_gain_ = 1.0f;
  float protection_target_gain_ = 1.0f;
  float high_pass_mix_ = 1.0f;
  uint64_t continuous_limiter_samples_ = 0;
  uint32_t coefficient_countdown_ = 0;
  uint32_t protection_countdown_ = 0;
  uint32_t coefficient_rebuild_count_ = 0;
  Diagnostics diagnostics_{};
  StereoBiquad *high_pass_;
  StereoBiquad *low_;
  StereoBiquad *mid_;
  StereoBiquad *high_;
};

} // namespace lunaredu::audio::dsp


