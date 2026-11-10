#include "lunaros_audio_dsp.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <dsps_biquad.h>
#include <dsps_biquad_gen.h>
#include <cstdlib>
#include <new>
#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#endif

namespace lunaredu::audio::dsp {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kLimiterThreshold = 25000.0f / 32768.0f;
constexpr float kSoftClipKnee = 26000.0f / 32768.0f;
constexpr float kSoftClipCeiling = 30000.0f / 32768.0f;

float db_to_gain(float db) { return std::pow(10.0f, db / 20.0f); }
float gain_to_db(float gain) {
  return 20.0f * std::log10(std::max(gain, 1.0e-9f));
}
float smooth_alpha(float milliseconds, uint32_t sample_rate) {
  return std::exp(-1.0f /
                  (std::max(milliseconds, 0.01f) * 0.001f * sample_rate));
}
bool finite(float value) { return std::isfinite(value); }

BiquadCoefficients coefficients(const float (&c)[5]) {
  return {c[0], c[1], c[2], c[3], c[4]};
}

BiquadCoefficients make_high_pass(float hz, uint32_t sample_rate) {
  float c[5];
  dsps_biquad_gen_hpf_f32(c, hz / sample_rate, 0.70710678f);
  return coefficients(c);
}

BiquadCoefficients make_peak(float hz, float q, float gain_db,
                             uint32_t sample_rate) {
  float c[5];
  // ESP-DSP 1.8.2's notch generator uses alpha in the denominator and
  // alpha*10^(gain/40) in the numerator. Map our RBJ peak gain/Q to that
  // convention; peakingEQ_f32 is a band-pass without a gain parameter.
  const float a = db_to_gain(gain_db * 0.5f);
  dsps_biquad_gen_notch_f32(c, hz / sample_rate, gain_db * 2.0f, q * a);
  return coefficients(c);
}

BiquadCoefficients make_shelf(float hz, float gain_db, bool high,
                              uint32_t sample_rate) {
  float c[5];
  if (high) {
    dsps_biquad_gen_highShelf_f32(c, hz / sample_rate, gain_db, 0.70710678f);
  } else {
    dsps_biquad_gen_lowShelf_f32(c, hz / sample_rate, gain_db, 0.70710678f);
  }
  return coefficients(c);
}

float approach(float value, float target, float amount) {
  return value + (target - value) * amount;
}

} // namespace

struct AudioProcessor::Biquad {
  BiquadCoefficients c{};
  float history[2]{};
  void set(const BiquadCoefficients &value) { c = value; }
  float process(float input) {
    float coef[]{c.b0, c.b1, c.b2, c.a1, c.a2};
    float output = 0.0f;
    // Keep sample-boundary parameter updates. ESP-DSP selects the target
    // kernel; host validation links its portable ANSI implementation.
    dsps_biquad_f32(&input, &output, 1, coef, history);
    if (finite(output)) return output;
    reset();
    return 0.0f;
  }
  bool process_block(float *samples,size_t count){
    float coef[]{c.b0,c.b1,c.b2,c.a1,c.a2};
    if(dsps_biquad_f32(samples,samples,int(count),coef,history)!=0)return false;
    for(size_t i=0;i<count;++i)if(!finite(samples[i]))return false;
    return finite(history[0])&&finite(history[1]);
  }
  void reset() { history[0] = history[1] = 0.0f; }
};

struct AudioProcessor::StereoBiquad {
  Biquad channel[2];
  void set(const BiquadCoefficients &c) {
    channel[0].set(c);
    channel[1].set(c);
  }
  void reset() { channel[0].reset(); channel[1].reset(); }
  void mirror_left() {
    std::copy_n(channel[0].history, 2, channel[1].history);
  }
};

Settings preset_settings(Preset preset) {
  Settings settings;
  settings.preset = preset;
  switch (preset) {
  case Preset::kVoice:
    settings.high_pass_hz = 100.0f;
    settings.low_gain_db = -3.0f;
    settings.mid_gain_db = 3.0f;
    settings.high_gain_db = 1.0f;
    break;
  case Preset::kMusic:
    settings.low_gain_db = 2.0f;
    settings.mid_gain_db = -1.0f;
    settings.high_gain_db = 2.0f;
    break;
  case Preset::kBassReduce:
    settings.high_pass_hz = 120.0f;
    settings.low_gain_db = -6.0f;
    break;
  case Preset::kCustom:
  case Preset::kFlat:
    break;
  }
  return settings;
}

const char *preset_name(Preset preset) {
  switch (preset) {
  case Preset::kFlat: return "flat";
  case Preset::kVoice: return "voice";
  case Preset::kMusic: return "music";
  case Preset::kBassReduce: return "bass_reduce";
  case Preset::kCustom: return "custom";
  }
  return "custom";
}

const char *channel_mode_name(ChannelMode mode) {
  switch (mode) {
  case ChannelMode::kStereo: return "stereo";
  case ChannelMode::kLeftOnly: return "left_only";
  case ChannelMode::kRightOnly: return "right_only";
  case ChannelMode::kAlternating: return "alternating";
  case ChannelMode::kMono: return "mono";
  case ChannelMode::kMute: return "mute";
  }
  return "mute";
}

bool parse_preset(const char *name, Preset &preset) {
  if (!name) return false;
  for (Preset candidate : {Preset::kFlat, Preset::kVoice, Preset::kMusic,
                           Preset::kBassReduce, Preset::kCustom}) {
    if (std::strcmp(name, preset_name(candidate)) == 0) {
      preset = candidate;
      return true;
    }
  }
  return false;
}

bool parse_channel_mode(const char *name, ChannelMode &mode) {
  if (!name) return false;
  for (ChannelMode candidate : {ChannelMode::kStereo, ChannelMode::kLeftOnly,
                                ChannelMode::kRightOnly,
                                ChannelMode::kAlternating,
                                ChannelMode::kMono, ChannelMode::kMute}) {
    if (std::strcmp(name, channel_mode_name(candidate)) == 0) {
      mode = candidate;
      return true;
    }
  }
  return false;
}

bool validate_settings(const Settings &s) {
  return finite(s.high_pass_hz) && s.high_pass_hz >= 40.0f &&
         s.high_pass_hz <= 200.0f && finite(s.low_frequency_hz) &&
         s.low_frequency_hz >= 60.0f && s.low_frequency_hz <= 300.0f &&
         finite(s.low_gain_db) && s.low_gain_db >= -12.0f &&
         s.low_gain_db <= 6.0f && finite(s.mid_frequency_hz) &&
         s.mid_frequency_hz >= 300.0f && s.mid_frequency_hz <= 4000.0f &&
         finite(s.mid_q) && s.mid_q >= 0.3f && s.mid_q <= 4.0f &&
         finite(s.mid_gain_db) && s.mid_gain_db >= -12.0f &&
         s.mid_gain_db <= 6.0f && finite(s.high_frequency_hz) &&
         s.high_frequency_hz >= 3000.0f && s.high_frequency_hz <= 12000.0f &&
         finite(s.high_gain_db) && s.high_gain_db >= -12.0f &&
         s.high_gain_db <= 6.0f;
}

float volume_gain(int volume) {
  const float normalized = std::clamp(volume, 0, kSystemMaxVolume) / 100.0f;
  return normalized * normalized;
}

bool biquad_is_stable(const BiquadCoefficients &c) {
  if (!finite(c.b0) || !finite(c.b1) || !finite(c.b2) || !finite(c.a1) ||
      !finite(c.a2)) return false;
  const float discriminant = c.a1 * c.a1 - 4.0f * c.a2;
  if (discriminant >= 0.0f) {
    const float root = std::sqrt(discriminant);
    return std::fabs((-c.a1 + root) * 0.5f) < 1.0f &&
           std::fabs((-c.a1 - root) * 0.5f) < 1.0f;
  }
  return c.a2 >= 0.0f && std::sqrt(c.a2) < 1.0f;
}

AudioProcessor::AudioProcessor()
    : high_pass_(nullptr), low_(nullptr), mid_(nullptr), high_(nullptr) {
  // These histories and coefficients are touched at the sample rate. Ordinary
  // malloc may prefer PSRAM even for tiny objects; reserve only this fixed hot
  // bank explicitly, independently of the queue and other cold owner storage.
#ifdef ESP_PLATFORM
  void *storage=heap_caps_malloc(4*sizeof(StereoBiquad),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
#else
  void *storage=std::malloc(4*sizeof(StereoBiquad));
#endif
  if(!storage)return;
  auto *filters=static_cast<StereoBiquad *>(storage);
  for(unsigned index=0;index<4;++index)new(filters+index) StereoBiquad;
  high_pass_=filters;low_=filters+1;mid_=filters+2;high_=filters+3;
}

AudioProcessor::~AudioProcessor() {
  if(!high_pass_)return;
  for(unsigned index=0;index<4;++index)high_pass_[index].~StereoBiquad();
#ifdef ESP_PLATFORM
  heap_caps_free(high_pass_);
#else
  std::free(high_pass_);
#endif
}

bool AudioProcessor::configure(uint32_t sample_rate, const Settings &settings) {
  if (!high_pass_ || sample_rate < 8000 || sample_rate > 48000 || !validate_settings(settings))
    return false;
  sample_rate_ = sample_rate;
  target_ = settings;
  current_ = settings;
  rebuild_runtime_constants();
  rebuild_coefficients();
  reset();
  return true;
}

void AudioProcessor::set_settings(const Settings &settings) {
  if (validate_settings(settings)) target_ = settings;
}
void AudioProcessor::set_volume(int volume) {
  volume_ = std::clamp(volume, 0, kSystemMaxVolume);
  volume_gain_ = volume_gain(volume_);
}
void AudioProcessor::set_channel_mode(ChannelMode mode) { channel_mode_ = mode; }

void AudioProcessor::request_mute(uint32_t fade_ms) {
  if (fade_state_ == FadeState::kMuted) return;
  begin_fade(FadeState::kRampDown, fade_ms);
}
void AudioProcessor::request_unmute(uint32_t fade_ms) {
  if (fade_state_ == FadeState::kPlaying) return;
  begin_fade(FadeState::kRampUp, fade_ms);
}

void AudioProcessor::begin_fade(FadeState state, uint32_t fade_ms) {
  fade_state_ = state;
  fade_start_gain_ = fade_gain_;
  fade_samples_elapsed_ = 0;
  fade_samples_total_ = std::max<uint32_t>(1, sample_rate_ * fade_ms / 1000);
  const float phase_step = kPi / static_cast<float>(fade_samples_total_);
  fade_phase_cos_ = 1.0f;
  fade_phase_sin_ = 0.0f;
  fade_step_cos_ = std::cos(phase_step);
  fade_step_sin_ = std::sin(phase_step);
}

bool AudioProcessor::parameters_pending() const {
  return
      std::fabs(current_.high_pass_hz - target_.high_pass_hz) > 0.001f ||
      std::fabs(current_.low_frequency_hz - target_.low_frequency_hz) >
          0.001f ||
      std::fabs(current_.low_gain_db - target_.low_gain_db) > 0.0001f ||
      std::fabs(current_.mid_frequency_hz - target_.mid_frequency_hz) >
          0.001f ||
      std::fabs(current_.mid_q - target_.mid_q) > 0.0001f ||
      std::fabs(current_.mid_gain_db - target_.mid_gain_db) > 0.0001f ||
      std::fabs(current_.high_frequency_hz - target_.high_frequency_hz) >
          0.001f ||
      std::fabs(current_.high_gain_db - target_.high_gain_db) > 0.0001f;
}
void AudioProcessor::update_smoothed_parameters() {
  if (coefficient_countdown_ > 0) {
    --coefficient_countdown_;
    return;
  }
  coefficient_countdown_ = 31;
  const bool parameters_changed=parameters_pending();
  current_.high_pass_enabled = target_.high_pass_enabled;
  if (!parameters_changed) return;
  const float block_alpha = parameter_smoothing_amount_;
  current_.high_pass_hz = approach(current_.high_pass_hz,
                                    target_.high_pass_hz, block_alpha);
  current_.low_frequency_hz = approach(current_.low_frequency_hz,
                                        target_.low_frequency_hz, block_alpha);
  current_.low_gain_db = approach(current_.low_gain_db, target_.low_gain_db,
                                   block_alpha);
  current_.mid_frequency_hz = approach(current_.mid_frequency_hz,
                                        target_.mid_frequency_hz, block_alpha);
  current_.mid_q = approach(current_.mid_q, target_.mid_q, block_alpha);
  current_.mid_gain_db = approach(current_.mid_gain_db, target_.mid_gain_db,
                                   block_alpha);
  current_.high_frequency_hz = approach(current_.high_frequency_hz,
                                         target_.high_frequency_hz, block_alpha);
  current_.high_gain_db = approach(current_.high_gain_db,
                                    target_.high_gain_db, block_alpha);
  rebuild_coefficients();
}

void AudioProcessor::rebuild_runtime_constants() {
  const float rate = static_cast<float>(sample_rate_);
  dc_feedback_ = std::exp(-2.0f * kPi * 10.0f / rate);
  parameter_smoothing_amount_ = 1.0f - std::exp(-32.0f / (0.050f * rate));
  high_pass_smoothing_amount_ = 1.0f - smooth_alpha(50.0f, sample_rate_);
  rms_alpha_ = smooth_alpha(500.0f, sample_rate_);
  thermal_attack_alpha_ = smooth_alpha(5000.0f, sample_rate_);
  thermal_release_alpha_ = smooth_alpha(30000.0f, sample_rate_);
  limiter_attack_alpha_ = smooth_alpha(1.0f, sample_rate_);
  limiter_release_alpha_ = smooth_alpha(150.0f, sample_rate_);
  protection_attack_multiplier_ = db_to_gain(-1.0f / rate);
  protection_release_multiplier_ = db_to_gain(0.25f / rate);
  protection_active_threshold_ = db_to_gain(-0.5f);
  volume_gain_ = volume_gain(volume_);
}

void AudioProcessor::rebuild_coefficients() {
  ++coefficient_rebuild_count_;
  effective_high_hz_ =
      std::min(current_.high_frequency_hz, 0.45f * sample_rate_);
  high_pass_->set(make_high_pass(current_.high_pass_hz, sample_rate_));
  low_->set(make_shelf(current_.low_frequency_hz, current_.low_gain_db, false,
                       sample_rate_));
  mid_->set(make_peak(current_.mid_frequency_hz, current_.mid_q,
                      current_.mid_gain_db, sample_rate_));
  high_->set(make_shelf(effective_high_hz_, current_.high_gain_db, true,
                        sample_rate_));
  const float positive_db = std::max(0.0f, current_.low_gain_db) +
                            std::max(0.0f, current_.mid_gain_db) +
                            std::max(0.0f, current_.high_gain_db);
  eq_pre_gain_ = db_to_gain(-positive_db);
  diagnostics_.eq_pre_gain = eq_pre_gain_;
}

void AudioProcessor::materialize_stereo_history() {
  if(!channels_symmetric_)return;
  dc_x_[1]=dc_x_[0];dc_y_[1]=dc_y_[0];
  if(high_pass_){
    high_pass_->mirror_left();low_->mirror_left();
    mid_->mirror_left();high_->mirror_left();
  }
  channels_symmetric_=false;
}

void AudioProcessor::process_filters(float &left, float &right) {
  if(!high_pass_){left=right=0;return;}
  const bool mono=channels_symmetric_ && left==right;
  // Synchronize before either side advances or new coefficients are applied.
  if(!mono)materialize_stereo_history();
  update_smoothed_parameters();
  const float dry_left = left;
  const float dry_right = right;
  const float wet_left = high_pass_->channel[0].process(left);
  const float wet_right = mono ? wet_left : high_pass_->channel[1].process(right);
  const float hp_target = target_.high_pass_enabled ? 1.0f : 0.0f;
  high_pass_mix_ = approach(high_pass_mix_, hp_target,
                            high_pass_smoothing_amount_);
  left = dry_left + (wet_left - dry_left) * high_pass_mix_;
  right = dry_right + (wet_right - dry_right) * high_pass_mix_;
  left = low_->channel[0].process(left);
  if(!mono)right = low_->channel[1].process(right);
  left = mid_->channel[0].process(left);
  if(!mono)right = mid_->channel[1].process(right);
  left = high_->channel[0].process(left);
  if(!mono)right = high_->channel[1].process(right);
  else {
    // Keep the equivalent right history implicit throughout a mono stream.
    // Copy it once if a later input actually diverges, not at every sample.
    right=left;
  }
}

float AudioProcessor::process_dc(float input, int channel) {
  const float output =
      input - dc_x_[channel] + dc_feedback_ * dc_y_[channel];
  dc_x_[channel] = input;
  dc_y_[channel] = finite(output) ? output : 0.0f;
  return dc_y_[channel];
}

float AudioProcessor::soft_clip(float input) {
  const float magnitude = std::fabs(input);
  if (magnitude <= kSoftClipKnee) return input;
  ++diagnostics_.soft_clip_samples;
  const float compressed = std::min(kSoftClipCeiling,
      kSoftClipKnee + (magnitude - kSoftClipKnee) * 0.25f);
  return std::copysign(compressed, input);
}

void AudioProcessor::process_common(int16_t left_in, int16_t right_in,
                                    float &left, float &right,
                                    float &pre_volume_mono, bool apply_volume,
                                    float external_gain) {
  left = left_in / 32768.0f;
  right = right_in / 32768.0f;
  diagnostics_.input_peak = std::max(
      diagnostics_.input_peak, std::max(std::fabs(left), std::fabs(right)));
  const bool mono=channels_symmetric_ && left_in==right_in;
  if(!mono)materialize_stereo_history();
  left = process_dc(left, 0);
  right = mono ? left : process_dc(right, 1);
  process_filters(left, right);
  left *= eq_pre_gain_;
  right *= eq_pre_gain_;
  pre_volume_mono = std::clamp(0.5f * (left + right), -1.0f, 1.0f);
  process_after_filters(left,right,apply_volume,external_gain);
}
void AudioProcessor::process_after_filters(float &left,float &right,bool apply_volume,float external_gain){
  const float gain = std::clamp(external_gain, 0.0f, 1.0f);
  if (apply_volume) {
    left *= volume_gain_;
    right *= volume_gain_;
  }
  left *= gain;
  right *= gain;

  const float instantaneous_power = 0.5f * (left * left + right * right);
  rms_power_ = rms_alpha_ * rms_power_ +
               (1.0f - rms_alpha_) * instantaneous_power;
  const float thermal_alpha = instantaneous_power > thermal_power_
                                  ? thermal_attack_alpha_
                                  : thermal_release_alpha_;
  thermal_power_ = thermal_alpha * thermal_power_ +
                   (1.0f - thermal_alpha) * instantaneous_power;
  if (protection_countdown_ == 0) {
    protection_countdown_ = 31;
    diagnostics_.rms = std::sqrt(std::max(0.0f, rms_power_));
    // Below -20 dB power the existing -14 dB protection threshold cannot be
    // reached. Preserve the envelope/history and recovery ramp, but avoid
    // software libm log/pow work for each 32 quiet speech samples.
    if(thermal_power_<=0.01f)protection_target_gain_=1.0f;
    else {
    const float thermal_rms =
        std::sqrt(std::max(0.0f, thermal_power_));
    const float rms_db = gain_to_db(thermal_rms);
    float target_protection_db = 0.0f;
    if (rms_db > -14.0f) {
      target_protection_db =
          -12.0f * std::clamp((rms_db + 14.0f) / 8.0f, 0.0f, 1.0f);
    }
    protection_target_gain_ = db_to_gain(target_protection_db);
    }
  } else {
    --protection_countdown_;
  }
  if (protection_target_gain_ < protection_gain_) {
    protection_gain_ =
        std::max(protection_target_gain_,
                 protection_gain_ * protection_attack_multiplier_);
  } else {
    protection_gain_ =
        std::min(protection_target_gain_,
                 protection_gain_ * protection_release_multiplier_);
  }
  diagnostics_.protection_gain = protection_gain_;
  diagnostics_.protection_active =
      protection_gain_ < protection_active_threshold_;
  left *= protection_gain_;
  right *= protection_gain_;

  process_linked_limiter(left, right);
  left = soft_clip(left);
  right = soft_clip(right);
}

bool AudioProcessor::process_pcm(const int16_t *input,int16_t *output,size_t frames,
    unsigned channels,bool apply_volume,float external_gain){
  if(!output||channels<1||channels>2||!high_pass_||frames>SIZE_MAX/channels)return false;
  constexpr size_t quantum=128;
  alignas(16) std::array<float,quantum> left{},right{},temporary{};
  for(size_t base=0;base<frames;){
    const auto count=std::min(quantum,frames-base);
    bool processed=false;
    if(!parameters_pending()){
      Biquad *filters[]{&high_pass_->channel[0],&high_pass_->channel[1],&low_->channel[0],&low_->channel[1],
        &mid_->channel[0],&mid_->channel[1],&high_->channel[0],&high_->channel[1]};
      float histories[8][2];
      for(size_t i=0;i<8;++i)std::copy_n(filters[i]->history,2,histories[i]);
      const float x[2]{dc_x_[0],dc_x_[1]},y[2]{dc_y_[0],dc_y_[1]};
      const auto old_mix=high_pass_mix_,old_peak=diagnostics_.input_peak;
      const auto old_countdown=coefficient_countdown_;
      const bool old_symmetric=channels_symmetric_,old_enabled=current_.high_pass_enabled;
      bool mono=channels_symmetric_;
      if(mono&&input&&channels==2)for(size_t i=0;i<count;++i)
        if(input[(base+i)*2]!=input[(base+i)*2+1]){mono=false;break;}
      if(!mono)materialize_stereo_history();
      for(size_t i=0;i<count;++i){
        const auto index=(base+i)*channels;
        const float l=input?input[index]/32768.0f:0;
        const float r=channels==1?l:input?input[index+1]/32768.0f:0;
        diagnostics_.input_peak=std::max(diagnostics_.input_peak,std::max(std::fabs(l),std::fabs(r)));
        left[i]=process_dc(l,0);right[i]=mono?left[i]:process_dc(r,1);
        update_smoothed_parameters(); // Preserve the existing sample countdown.
      }
      bool valid=true;
      const float hp_target=target_.high_pass_enabled?1.0f:0.0f;
      for(unsigned channel=0;channel<(mono?1u:2u);++channel){
        auto &samples=channel?right:left;
        std::copy_n(samples.data(),count,temporary.data());
        if(!high_pass_->channel[channel].process_block(temporary.data(),count)){valid=false;break;}
        float mix=old_mix;
        for(size_t i=0;i<count;++i){
          mix=approach(mix,hp_target,high_pass_smoothing_amount_);
          samples[i]+= (temporary[i]-samples[i])*mix;
        }
        if(!channel)high_pass_mix_=mix;
        if(!low_->channel[channel].process_block(samples.data(),count)||
           !mid_->channel[channel].process_block(samples.data(),count)||
           !high_->channel[channel].process_block(samples.data(),count)){valid=false;break;}
      }
      if(valid){
        for(size_t i=0;i<count;++i){
          float l=left[i]*eq_pre_gain_,r=(mono?left[i]:right[i])*eq_pre_gain_;
          process_after_filters(l,r,apply_volume,external_gain);
          finalize_output(l,r);
          output[(base+i)*channels]=quantize(channels==1?0.5f*(l+r):l);
          if(channels==2)output[(base+i)*2+1]=quantize(r);
        }
        processed=true;
      }else{
        // A malformed/nonfinite state must keep the scalar reset semantics.
        // Restore before retry; never advance the same filter history twice.
        for(size_t i=0;i<8;++i)std::copy_n(histories[i],2,filters[i]->history);
        std::copy_n(x,2,dc_x_);std::copy_n(y,2,dc_y_);
        high_pass_mix_=old_mix;diagnostics_.input_peak=old_peak;coefficient_countdown_=old_countdown;
        channels_symmetric_=old_symmetric;current_.high_pass_enabled=old_enabled;
      }
    }
    if(!processed)for(size_t i=0;i<count;++i){
      const auto index=(base+i)*channels;
      const int16_t l=input?input[index]:0,r=channels==1?l:input?input[index+1]:0;
      float a=0,b=0,reference=0;
      process_common(l,r,a,b,reference,apply_volume,external_gain);finalize_output(a,b);
      output[index]=quantize(channels==1?0.5f*(a+b):a);
      if(channels==2)output[index+1]=quantize(b);
    }
    base+=count;
  }
  return true;
}

void AudioProcessor::process_linked_limiter(float &left, float &right) {
  const float linked_peak = std::max(std::fabs(left), std::fabs(right));
  const float requested_gain = linked_peak > kLimiterThreshold
                                   ? kLimiterThreshold / linked_peak
                                   : 1.0f;
  const float limiter_alpha = requested_gain < limiter_gain_
                                  ? limiter_attack_alpha_
                                  : limiter_release_alpha_;
  if (requested_gain < limiter_gain_ && limiter_gain_ >= 0.9999f)
    ++diagnostics_.limiter_events;
  limiter_gain_ = requested_gain + limiter_alpha * (limiter_gain_ - requested_gain);
  if (limiter_gain_ < 0.9999f) {
    ++diagnostics_.limiter_samples;
    ++continuous_limiter_samples_;
    // Convert the diagnostic counter on observation, not once per sample.
    // This removes a software 64-bit division from the S3 sample loop.
  } else {
    continuous_limiter_samples_ = 0;

  }
  diagnostics_.limiter_gain = limiter_gain_;
  if (limiter_gain_ < minimum_limiter_gain_) {
    minimum_limiter_gain_ = limiter_gain_;
    diagnostics_.maximum_limiter_reduction_db =
        -gain_to_db(minimum_limiter_gain_);
  }
  left *= limiter_gain_;
  right *= limiter_gain_;
}

float AudioProcessor::next_fade_gain() {
  if (fade_state_ != FadeState::kRampUp && fade_state_ != FadeState::kRampDown)
    return fade_gain_;
  ++fade_samples_elapsed_;
  const float next_cos = fade_phase_cos_ * fade_step_cos_ -
                         fade_phase_sin_ * fade_step_sin_;
  const float next_sin = fade_phase_sin_ * fade_step_cos_ +
                         fade_phase_cos_ * fade_step_sin_;
  fade_phase_cos_ = next_cos;
  fade_phase_sin_ = next_sin;
  const float raised_cosine = 0.5f - 0.5f * fade_phase_cos_;
  if (fade_state_ == FadeState::kRampUp) {
    fade_gain_ = fade_start_gain_ + (1.0f - fade_start_gain_) * raised_cosine;
    if (fade_samples_elapsed_ >= fade_samples_total_) {
      fade_gain_ = 1.0f;
      fade_state_ = FadeState::kPlaying;
    }
  } else {
    fade_gain_ = fade_start_gain_ * (1.0f - raised_cosine);
    if (fade_samples_elapsed_ >= fade_samples_total_) {
      fade_gain_ = 0.0f;
      fade_state_ = FadeState::kMuted;
    }
  }
  return fade_gain_;
}

void AudioProcessor::finalize_output(float &left, float &right) {
  const float fade = next_fade_gain();
  left *= fade;
  right *= fade;
  const float mono = 0.25f * (left + right); // -6 dB mono sum.
  switch (channel_mode_) {
  case ChannelMode::kLeftOnly: right = 0.0f; break;
  case ChannelMode::kRightOnly: left = 0.0f; break;
  case ChannelMode::kAlternating:
    if ((routed_samples_ / sample_rate_) % 2 == 0) right = 0.0f;
    else left = 0.0f;
    break;
  case ChannelMode::kMono: left = right = mono; break;
  case ChannelMode::kMute: left = right = 0.0f; break;
  case ChannelMode::kStereo: break;
  }
  ++routed_samples_;
  if (!finite(left) || !finite(right)) {
    left = right = 0.0f;
    ++diagnostics_.invalid_samples;
    request_mute(1);
  }
  diagnostics_.output_peak = std::max(
      diagnostics_.output_peak, std::max(std::fabs(left), std::fabs(right)));
}

int16_t AudioProcessor::quantize(float sample) const {
  if (!finite(sample)) return 0;
  const float scaled = std::clamp(sample, -1.0f, 0.9999695f) * 32768.0f;
  int value = static_cast<int>(scaled);
  const float fraction = scaled - value;
  // Preserve lround's ties-away-from-zero rule using the S3's float-to-int
  // instruction. Adding 0.5 before truncation can misround near a half-step.
  value += (fraction >= .5f) - (fraction <= -.5f);
  return static_cast<int16_t>(value);
}

Diagnostics AudioProcessor::diagnostics() const {
  auto result=diagnostics_;
  result.continuous_limiter_ms=static_cast<uint32_t>(continuous_limiter_samples_*1000ULL/sample_rate_);
  return result;
}

void AudioProcessor::reset() {
  if(!high_pass_)return;
  channels_symmetric_=true;
  high_pass_->reset(); low_->reset(); mid_->reset(); high_->reset();
  dc_x_[0] = dc_x_[1] = dc_y_[0] = dc_y_[1] = 0.0f;
  limiter_gain_ = 1.0f;
  minimum_limiter_gain_ = 1.0f;
  rms_power_ = 0.0f;
  thermal_power_ = 0.0f;
  protection_gain_ = 1.0f;
  protection_target_gain_ = 1.0f;
  high_pass_mix_ = target_.high_pass_enabled ? 1.0f : 0.0f;
  continuous_limiter_samples_ = 0;
  fade_state_ = FadeState::kMuted;
  fade_gain_ = 0.0f;
  fade_phase_cos_ = 1.0f;
  fade_phase_sin_ = 0.0f;
  routed_samples_ = 0;
  coefficient_countdown_ = 0;
  protection_countdown_ = 0;
  diagnostics_ = {};
  diagnostics_.eq_pre_gain = eq_pre_gain_;
}

BiquadCoefficients AudioProcessor::high_pass_coefficients() const { return high_pass_?high_pass_->channel[0].c:BiquadCoefficients{}; }
BiquadCoefficients AudioProcessor::low_coefficients() const { return low_?low_->channel[0].c:BiquadCoefficients{}; }
BiquadCoefficients AudioProcessor::mid_coefficients() const { return mid_?mid_->channel[0].c:BiquadCoefficients{}; }
BiquadCoefficients AudioProcessor::high_coefficients() const { return high_?high_->channel[0].c:BiquadCoefficients{}; }

} // namespace lunaredu::audio::dsp


