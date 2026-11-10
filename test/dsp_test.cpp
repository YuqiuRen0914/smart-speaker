#include "smart_speaker_dsp.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>

using smart_speaker::dsp::AudioProcessor;
using smart_speaker::dsp::Settings;

static float filtered_rms(float frequency_hz, bool high_pass) {
  AudioProcessor processor;
  Settings settings;
  settings.high_pass_enabled = high_pass;
  assert(processor.configure(48000, settings));
  double power = 0.0;
  for (int i = 0; i < 96000; ++i) {
    float left = std::sin(2.0f * 3.14159265358979323846f * frequency_hz * i /
                          48000.0f);
    float right = left;
    processor.process_filters(left, right);
    if (i >= 48000) power += static_cast<double>(left) * left;
  }
  return std::sqrt(static_cast<float>(power / 48000.0));
}

static void test_coefficients_and_extremes() {
  for (uint32_t rate : {8000U, 11025U, 16000U, 22050U, 32000U, 44100U,
                        48000U}) {
    AudioProcessor processor;
    Settings settings;
    settings.low_gain_db = 6.0f;
    settings.mid_gain_db = 6.0f;
    settings.high_gain_db = 6.0f;
    assert(processor.configure(rate, settings));
    assert(smart_speaker::dsp::biquad_is_stable(
        processor.high_pass_coefficients()));
    assert(smart_speaker::dsp::biquad_is_stable(processor.low_coefficients()));
    assert(smart_speaker::dsp::biquad_is_stable(processor.mid_coefficients()));
    assert(smart_speaker::dsp::biquad_is_stable(processor.high_coefficients()));
    assert(processor.effective_high_frequency_hz() <= rate * 0.45f + 0.01f);
    processor.set_volume(100);
    processor.request_unmute(1);
    for (uint32_t i = 0; i < rate; ++i) {
      float left = 0.0f;
      float right = 0.0f;
      float pre = 0.0f;
      processor.process_common(i & 1 ? INT16_MAX : INT16_MIN,
                               i & 1 ? INT16_MIN : INT16_MAX,
                               left, right, pre);
      processor.finalize_output(left, right);
      assert(std::isfinite(left) && std::isfinite(right));
      (void)processor.quantize(left);
      (void)processor.quantize(right);
    }
  }
}

static void test_unchanged_parameters_do_not_rebuild_every_sample() {
  AudioProcessor processor;
  Settings settings;
  assert(processor.configure(44100, settings));
  const uint32_t initial_rebuilds = processor.coefficient_rebuild_count();
  for (int i = 0; i < 44100; ++i) {
    float left = 0.1f;
    float right = -0.1f;
    processor.process_filters(left, right);
  }
  assert(processor.coefficient_rebuild_count() == initial_rebuilds);

  settings.mid_gain_db = 3.0f;
  processor.set_settings(settings);
  for (int i = 0; i < 4410; ++i) {
    float left = 0.1f;
    float right = -0.1f;
    processor.process_filters(left, right);
  }
  assert(processor.coefficient_rebuild_count() > initial_rebuilds);
  assert(processor.coefficient_rebuild_count() < initial_rebuilds + 140);
}

static void test_volume_limiter_and_fade() {
  assert(smart_speaker::dsp::volume_gain(0) == 0.0f);
  assert(std::fabs(smart_speaker::dsp::volume_gain(70) - 0.49f) < 1.0e-6f);
  assert(smart_speaker::dsp::volume_gain(100) <= 0.49f);

  AudioProcessor limiter;
  Settings settings;
  assert(limiter.configure(48000, settings));
  float left = 1.0f;
  float right = 0.25f;
  limiter.process_linked_limiter(left, right);
  assert(left < 1.0f);
  assert(std::fabs(left / right - 4.0f) < 1.0e-4f);
  assert(limiter.diagnostics().limiter_events == 1);

  AudioProcessor fade;
  assert(fade.configure(10000, settings));
  fade.request_unmute(40);
  float previous = -1.0f;
  for (int i = 0; i < 400; ++i) {
    float l = 1.0f;
    float r = 1.0f;
    fade.finalize_output(l, r);
    assert(l >= previous);
    previous = l;
  }
  assert(fade.fade_state() == smart_speaker::dsp::FadeState::kPlaying);
  fade.request_mute(40);
  previous = 2.0f;
  for (int i = 0; i < 400; ++i) {
    float l = 1.0f;
    float r = 1.0f;
    fade.finalize_output(l, r);
    assert(l <= previous + 1.0e-6f);
    previous = l;
  }
  assert(fade.muted());
  assert(fade.fade_gain() == 0.0f);
}

static void test_voice_path_bypasses_music_volume() {
  AudioProcessor normal;
  AudioProcessor voice;
  Settings settings;
  settings.high_pass_enabled = false;
  assert(normal.configure(16000, settings));
  assert(voice.configure(16000, settings));
  normal.set_volume(10);
  voice.set_volume(10);

  float normal_left = 0.0f;
  float normal_right = 0.0f;
  float normal_pre = 0.0f;
  normal.process_common(16000, 16000, normal_left, normal_right, normal_pre);

  float voice_left = 0.0f;
  float voice_right = 0.0f;
  float voice_pre = 0.0f;
  voice.process_common(16000, 16000, voice_left, voice_right, voice_pre,
                       false);

  assert(std::fabs(normal_left) < 0.01f);
  assert(voice_left > normal_left * 20.0f);
  assert(std::fabs(voice_left - voice_right) < 1.0e-6f);
}

static void test_thermal_and_routing() {
  AudioProcessor processor;
  Settings settings;
  settings.high_pass_enabled = false;
  assert(processor.configure(8000, settings));
  processor.set_volume(70);
  for (int i = 0; i < 8000 * 12; ++i) {
    float l, r, pre;
    const int16_t sample = (i & 1) ? INT16_MAX : INT16_MIN;
    processor.process_common(sample, sample, l, r, pre);
  }
  assert(processor.diagnostics().protection_gain < 0.9f);
  assert(processor.diagnostics().protection_active);

  processor.request_unmute(1);
  for (int i = 0; i < 8; ++i) {
    float l = 0.4f;
    float r = -0.2f;
    processor.finalize_output(l, r);
  }
  struct Expected { smart_speaker::dsp::ChannelMode mode; float l; float r; };
  for (Expected expected : {
           Expected{smart_speaker::dsp::ChannelMode::kStereo, 0.4f, -0.2f},
           Expected{smart_speaker::dsp::ChannelMode::kLeftOnly, 0.4f, 0.0f},
           Expected{smart_speaker::dsp::ChannelMode::kRightOnly, 0.0f, -0.2f},
           Expected{smart_speaker::dsp::ChannelMode::kMono, 0.05f, 0.05f},
           Expected{smart_speaker::dsp::ChannelMode::kMute, 0.0f, 0.0f},
       }) {
    processor.set_channel_mode(expected.mode);
    float l = 0.4f;
    float r = -0.2f;
    processor.finalize_output(l, r);
    assert(std::fabs(l - expected.l) < 1.0e-5f);
    assert(std::fabs(r - expected.r) < 1.0e-5f);
  }

  AudioProcessor alternating;
  assert(alternating.configure(8000, settings));
  alternating.request_unmute(1);
  for (int i = 0; i < 8; ++i) {
    float l = 0.4f, r = 0.2f;
    alternating.finalize_output(l, r);
  }
  alternating.set_channel_mode(
      smart_speaker::dsp::ChannelMode::kAlternating);
  float l = 0.4f, r = 0.2f;
  alternating.finalize_output(l, r);
  assert(l > 0.39f && r == 0.0f);
  for (int i = 0; i < 8000; ++i) {
    l = 0.4f; r = 0.2f;
    alternating.finalize_output(l, r);
  }
  assert(l == 0.0f && r > 0.19f);
}

int main() {
  AudioProcessor processor;
  // Exact compatibility at every PCM half-step and its adjacent floats.
  for (int n=-32768;n<=32767;++n) {
    const float half=(n+.5f)/32768.0f;
    for (float v : {std::nextafter(half,-INFINITY),half,std::nextafter(half,INFINITY)}) {
      // Round the float-scaled sample in double precision: older MinGW
      // lroundf incorrectly rounds the float immediately above -0.5 to -1.
      const auto expected=std::clamp<long>(std::lround(static_cast<double>(std::clamp(v,-1.0f,.9999695f)*32768)),-32768,32767);
      assert(processor.quantize(v)==expected);
    }
  }
  assert(processor.quantize(NAN)==0 && processor.quantize(INFINITY)==0);
  assert(processor.quantize(-2)==-32768 && processor.quantize(2)==32767);
  Settings settings;
  settings.high_pass_enabled = false;
  settings.low_gain_db = 0.0f;
  settings.mid_gain_db = 0.0f;
  settings.high_gain_db = 0.0f;
  assert(processor.configure(48000, settings));

  float max_error = 0.0f;
  for (int i = 0; i < 4096; ++i) {
    const float sample = 0.25f * std::sin(2.0f * 3.14159265358979323846f *
                                         1000.0f * i / 48000.0f);
    float left = sample;
    float right = -sample;
    processor.process_filters(left, right);
    if (i > 256) {
      max_error = std::max(max_error, std::fabs(left - sample));
      max_error = std::max(max_error, std::fabs(right + sample));
    }
  }
  assert(max_error < 1.0e-4f);

  const float passband = filtered_rms(1000.0f, true);
  const float low30 = filtered_rms(30.0f, true);
  const float low40 = filtered_rms(40.0f, true);
  assert(passband > 0.69f);
  assert(low30 < passband * 0.18f);
  assert(low40 < passband * 0.28f);
  test_coefficients_and_extremes();
  test_unchanged_parameters_do_not_rebuild_every_sample();
  test_volume_limiter_and_fade();
  test_voice_path_bypasses_music_volume();
  test_thermal_and_routing();
  std::puts("dsp_test: PASS");
}
