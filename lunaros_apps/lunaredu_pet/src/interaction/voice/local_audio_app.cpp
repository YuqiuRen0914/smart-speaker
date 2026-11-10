#include "interaction/voice/resident_audio.h"
#include "interaction/voice/local_audio_app.h"

#include <cmath>
#include <vector>

#include "lunaredu_audio_bridge.h"
#include "lunaredu_lunaros/persistence/system_audio.h"
#include <esp_log.h>

namespace {

constexpr const char *kTag = "pc_local_audio";

} // namespace

void setup_local_audio_app() {
  const bool ok = lunaredu::audio::setup_local_pcm_audio_bridge();
  if (!ok) {
    const auto status = lunaredu::audio::local_pcm_audio_bridge_status();
    if (!status.last_error.empty()) {
      ESP_LOGW(kTag, "%s", status.last_error.c_str());
    }
  } else {
    (void)lunaredu::lunaros::apply_system_volume();
  }
}

bool local_audio_app_play_wake_chime() {
  const auto status = lunaredu::audio::local_pcm_audio_bridge_status();
  if (!status.initialized) {
    return false;
  }

  const bool output_was_enabled = status.output_enabled;
  if (!output_was_enabled &&
      !lunaredu::pet::conversation::audio_io::enable_output(true)) {
    return false;
  }

  const uint32_t sample_rate =
      status.output_sample_rate > 0 ? static_cast<uint32_t>(status.output_sample_rate)
                                    : 24000U;
  struct ToneSegment {
    float freq_hz;
    uint32_t duration_ms;
    uint32_t gap_ms;
  };
  constexpr ToneSegment kSegments[] = {
      {1046.5f, 70U, 18U},
      {1318.5f, 90U, 0U},
  };
  constexpr float kAmplitude = 0.22f;
  constexpr float kTwoPi = 6.28318530718f;

  for (const auto &segment : kSegments) {
    const size_t sample_count =
        static_cast<size_t>((sample_rate * segment.duration_ms) / 1000U);
    std::vector<int16_t> samples(sample_count, 0);
    for (size_t index = 0; index < sample_count; ++index) {
      const float phase = kTwoPi * segment.freq_hz *
                          (static_cast<float>(index) / static_cast<float>(sample_rate));
      samples[index] = static_cast<int16_t>(std::sin(phase) * 32767.0f * kAmplitude);
    }
    if (!lunaredu::pet::conversation::audio_io::write_output(
            samples.data(), samples.size(), 120)) {
      if (!output_was_enabled) {
        lunaredu::pet::conversation::audio_io::enable_output(false);
      }
      return false;
    }

    if (segment.gap_ms > 0) {
      const size_t gap_samples =
          static_cast<size_t>((sample_rate * segment.gap_ms) / 1000U);
      std::vector<int16_t> silence(gap_samples, 0);
      if (!lunaredu::pet::conversation::audio_io::write_output(
              silence.data(), silence.size(), 120)) {
        if (!output_was_enabled) {
          lunaredu::pet::conversation::audio_io::enable_output(false);
        }
        return false;
      }
    }
  }

  if (!output_was_enabled) {
    lunaredu::pet::conversation::audio_io::enable_output(false);
  }
  return true;
}
