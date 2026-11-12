#include "smart_speaker_music_spectrum.h"
#include <cassert>
#include <iostream>
#include <thread>
using namespace smart_speaker::music;
int main() {
  SpectrumAnalyzer analyzer;
  PcmWindow pcm;
  for (uint32_t rate : {8000, 16000, 22050, 32000, 44100, 48000}) {
    pcm.rate = rate;
    for (float hz : {250.f, 1000.f, 2500.f}) {
      for (size_t i = 0; i < kSamples; ++i) {
        pcm.left[i] = int16_t(24000 * std::sin(6.283185307f * hz * i / rate));
        pcm.right[i] = -pcm.left[i];
      }
      const auto spectrum = analyzer.analyze(pcm);
      const int peak = int(std::max_element(spectrum.begin(), spectrum.end()) - spectrum.begin());
      const int expected = int(std::log(hz / 50.f) / std::log(320.f) * kBands);
      assert(std::abs(peak - expected) <= 1 && spectrum[peak] > .7f);
      const auto stereo = spectrum;
      pcm.left.fill(0); // right-only tracks must still animate
      assert(analyzer.analyze(pcm) == stereo);
      pcm.right.fill(0);
      for (auto x : analyzer.analyze(pcm)) assert(x == 0);
      pcm.left.fill(3000); pcm.right.fill(-3000); // DC is not music
      for (auto x : analyzer.analyze(pcm)) assert(x == 0);
    }
  }
  TapBuffers storage;
  SpectrumTap tap(storage);
  std::array<int16_t, kSamples> left{}, right{};
  left.fill(1234); right.fill(-1234);
  tap.push(left.data(), right.data(), kSamples, 44100, 1, 1000);
  assert(!tap.take(pcm, 1000));
  tap.enable(true);
  tap.push(left.data(), right.data(), 512, 44100, 1, 1000);
  assert(!tap.take(pcm, 1000));
  tap.invalidate(); // interrupted old partial window must not cross a track reset
  tap.push(left.data(), right.data(), 512, 44100, 2, 1020);
  assert(!tap.take(pcm, 1020));
  tap.push(left.data(), right.data(), 512, 44100, 2, 1040);
  assert(tap.take(pcm, 1040) && pcm.track == 2 && pcm.left[1023] == 1234);
  assert(!tap.take(pcm, 1040)); // consumed once
  tap.push(left.data(), right.data(), kSamples, 48000, 2, 1100);
  assert(!tap.take(pcm, 1281)); // stale frames never revive animation
  tap.enable(false); tap.enable(true);
  assert(!tap.take(pcm, 1100)); // old mailbox rejected on reentry
  tap.push(left.data(), right.data(), kSamples, 48000, 2, UINT32_MAX - 30);
  assert(tap.take(pcm, 40)); // timestamp wrap
  tap.push(left.data(), right.data(), 512, 48000, 2, 100);
  tap.push(left.data(), right.data(), 512, 16000, 2, 120);
  assert(!tap.take(pcm, 120)); // rate changes clear partial windows
  tap.push(left.data(), right.data(), 512, 16000, 2, 140);
  assert(tap.take(pcm, 140) && pcm.rate == 16000);

  std::atomic<bool> done{false};
  std::thread producer([&] {
    for (int i = 0; i < 20000; ++i)
      tap.push(left.data(), right.data(), kSamples, 44100, 3, 200);
    done = true;
  });
  while (!done) if (tap.take(pcm, 200)) {
    assert(pcm.track == 3);
    for (size_t i = 0; i < kSamples; ++i)
      assert(pcm.left[i] == 1234 && pcm.right[i] == -1234);
  }
  producer.join();
  Motion motion;
  Bands target; target.fill(.8f);
  for (uint32_t t = 0; t <= 1000; t += 40) motion.update(t, true, target);
  assert(motion.angle() > .1f && motion.levels()[0] > .7f);
  for (uint32_t t = 1040; t <= 4000; t += 40) motion.update(t, false, target);
  const auto angle = motion.angle();
  assert(!motion.update(4040, false, target));
  assert(motion.angle() == angle && motion.levels()[0] == 0);
  Motion wrap; wrap.update(UINT32_MAX - 20, true, target);
  wrap.update(20, true, target); assert(wrap.angle() > 0 && wrap.angle() < .01f);
  std::cout << "Music spectrum: frequency, stereo, silence, mailbox lifecycle/concurrency, pause and clock wrap PASS\n";
}
