#include "smart_speaker_wood_fish_sound.h"
#include "smart_speaker_dsp.h"
#include <cassert>
#include <cstdio>
using namespace smart_speaker;
int main() {
  WoodFishStrikeGate gate;
  assert(!gate.poll(true, 30, 100)); // No replay on entry.
  assert(!gate.poll(true, 31, 110));
  assert(!gate.poll(true, 32, 130)); // Rapid hits coalesce without delaying impact.
  assert(gate.poll(true, 33, 175));
  assert(!gate.poll(true, 33, 200)); // No duplicate playback.
  assert(!gate.poll(true, 34, 210));
  assert(!gate.poll(false, 34, 220)); // Assistant, PCM, page exit or failure cancels.
  assert(!gate.poll(true, 40, 400));
  assert(!gate.poll(true, 40, 600));
  assert(!gate.poll(true, 41, UINT32_MAX - 30));
  assert(gate.poll(true, 41, 40)); // Clock wrap.

  for (uint32_t rate : {8000U, 16000U, 22050U, 44100U, 48000U}) {
    WoodFishSound sound;
    assert(!sound.active() && sound.next() == 0);
    sound.strike(rate);
    double early = 0, late = 0;
    int peak = 0;
    for (uint32_t i = 0; i < rate / 5; ++i) {
      const int sample = sound.next();
      peak = std::max(peak, std::abs(sample));
      if (i < rate / 20) early += double(sample) * sample;
      if (i >= rate * 3 / 20) late += double(sample) * sample;
    }
    assert(peak > 1000 && peak < 10000);
    assert(early > late * 100 && !sound.active() && sound.next() == 0);
    sound.strike(rate);
    for (int i = 0; i < 100; ++i) (void)sound.next();
    sound.strike(rate); // A new hit restarts deterministically, no accumulated voices.
    assert(sound.next() == 0);
    sound.cancel();
    assert(!sound.active() && sound.next() == 0);
  }
  // A transient can sound while music remains paused. Channel mute and user
  // volume still apply, and the effect must never unmute subsequent music.
  for (int volume : {0, 50}) {
    dsp::AudioProcessor dsp;
    assert(dsp.configure(44100, dsp::preset_settings(dsp::Preset::kFlat)));
    dsp.set_volume(volume);
    WoodFishSound sound; sound.strike(44100);
    float peak = 0;
    for (int i = 0; i < 9000; ++i) {
      const auto sample = sound.next();
      float left = 0, right = 0, pre = 0;
      dsp.process_common(sample, sample, left, right, pre);
      dsp.finalize_output(left, right, false);
      peak = std::max(peak, std::abs(left));
      assert(dsp.muted());
    }
    assert(volume == 0 ? peak == 0 : peak > .0001f);
    dsp.set_channel_mode(dsp::ChannelMode::kMute);
    float left = .3f, right = .3f;
    dsp.finalize_output(left, right, false);
    assert(left == 0 && right == 0);
    dsp.set_channel_mode(dsp::ChannelMode::kStereo);
    left = right = .3f;
    dsp.finalize_output(left, right);
    assert(left == 0 && right == 0);
  }
  puts("Wood fish sound: finite decay, sample rates, retrigger, cancel, volume, channel mute and paused music isolation PASS");
}
