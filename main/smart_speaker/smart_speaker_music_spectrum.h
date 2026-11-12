#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace smart_speaker::music {
constexpr size_t kSamples = 1024;
constexpr size_t kBands = 16;
using Bands = std::array<float, kBands>;
struct PcmWindow {
  std::array<int16_t, kSamples> left{}, right{};
  uint32_t rate = 0, track = 0, epoch = 0, updated_ms = 0, sequence = 0;
};
struct TapBuffers { PcmWindow building, published; };

// One audio producer, one display consumer. Neither owner waits for the other:
// a busy mailbox drops a visual frame, never PCM. Large buffers live in PSRAM
// on the device; the small atomic control object stays in internal memory.
class SpectrumTap {
 public:
  explicit SpectrumTap(TapBuffers &buffers) : buffers_(buffers) {}
  void enable(bool enabled) {
    auto old = epoch_.load(std::memory_order_relaxed);
    while (bool(old & 1U) != enabled &&
        !epoch_.compare_exchange_weak(old, old + 1, std::memory_order_acq_rel)) {}
  }
  // Invalidates pending samples even if the page remains enabled (track reset).
  void invalidate() { epoch_.fetch_add(2, std::memory_order_acq_rel); }
  void discard() { filled_ = 0; } // audio producer only
  void push(const int16_t *left, const int16_t *right, size_t frames,
            uint32_t rate, uint32_t track, uint32_t now) {
    const auto epoch = epoch_.load(std::memory_order_acquire);
    auto &b = buffers_.building;
    if (!(epoch & 1U) || rate < 8000 || rate > 48000) { filled_ = 0; return; }
    if (b.epoch != epoch || b.rate != rate || b.track != track) {
      filled_ = 0; b.epoch = epoch; b.rate = rate; b.track = track;
    }
    for (size_t i = 0; i < frames; ++i) {
      b.left[filled_] = left[i]; b.right[filled_++] = right[i];
      if (filled_ != kSamples) continue;
      b.updated_ms = now; b.sequence = ++sequence_;
      if (!busy_.test_and_set(std::memory_order_acquire)) {
        buffers_.published = b;
        busy_.clear(std::memory_order_release);
      }
      filled_ = 0;
    }
  }
  bool take(PcmWindow &out, uint32_t now) {
    if (busy_.test_and_set(std::memory_order_acquire)) return false;
    const auto epoch = epoch_.load(std::memory_order_acquire);
    const auto &b = buffers_.published;
    const bool valid = (epoch & 1U) && b.epoch == epoch &&
        b.sequence != consumed_ && uint32_t(now - b.updated_ms) <= 180;
    if (valid) { out = b; consumed_ = b.sequence; }
    busy_.clear(std::memory_order_release);
    return valid;
  }
 private:
  TapBuffers &buffers_;
  std::atomic<uint32_t> epoch_{0};
  std::atomic_flag busy_ = ATOMIC_FLAG_INIT;
  size_t filled_ = 0;
  uint32_t sequence_ = 0, consumed_ = 0;
};

// Display-owner-only Hann-windowed radix-2 FFT; no allocations or SDK globals.
// Analyze the louder complete channel, so opposite-phase stereo never cancels.
// Values are visual dBFS magnitudes, not a calibrated measurement instrument.
class SpectrumAnalyzer {
 public:
  Bands analyze(const PcmWindow &pcm) {
    Bands result{};
    if (pcm.rate < 8000 || pcm.rate > 48000) return result;
    float l = 0, r = 0;
    for (size_t i = 0; i < kSamples; ++i) {
      l += float(pcm.left[i]) * pcm.left[i];
      r += float(pcm.right[i]) * pcm.right[i];
    }
    const auto &samples = r > l ? pcm.right : pcm.left;
    float mean = 0;
    for (auto x : samples) mean += x / float(kSamples);
    constexpr float pi = 3.14159265358979323846f;
    if (!window_ready_) {
      for (size_t i = 0; i < kSamples; ++i)
        window_[i] = .5f - .5f * std::cos(2 * pi * i / (kSamples - 1));
      window_ready_ = true;
    }
    for (size_t i = 0; i < kSamples; ++i) {
      real_[i] = (samples[i] - mean) / 32768.f *
          window_[i];
      imag_[i] = 0;
    }
    for (size_t i = 1, j = 0; i < kSamples; ++i) {
      size_t bit = kSamples >> 1;
      for (; j & bit; bit >>= 1) j ^= bit;
      j ^= bit;
      if (i < j) { std::swap(real_[i], real_[j]); std::swap(imag_[i], imag_[j]); }
    }
    for (size_t length = 2; length <= kSamples; length <<= 1) {
      const float wr = std::cos(-2 * pi / length), wi = std::sin(-2 * pi / length);
      for (size_t i = 0; i < kSamples; i += length) {
        float ar = 1, ai = 0;
        for (size_t j = 0; j < length / 2; ++j) {
          const auto a = i + j, b = a + length / 2;
          const float br = real_[b] * ar - imag_[b] * ai;
          const float bi = real_[b] * ai + imag_[b] * ar;
          real_[b] = real_[a] - br; imag_[b] = imag_[a] - bi;
          real_[a] += br; imag_[a] += bi;
          const float next = ar * wr - ai * wi;
          ai = ar * wi + ai * wr; ar = next;
        }
      }
    }
    for (size_t band = 0; band < kBands; ++band) {
      const float low = 50.f * std::pow(320.f, float(band) / kBands);
      const float high = 50.f * std::pow(320.f, float(band + 1) / kBands);
      if (low >= pcm.rate / 2.f) continue;
      const size_t first = std::max<size_t>(1, std::lround(low * kSamples / pcm.rate));
      const size_t end = std::min<size_t>(kSamples / 2, std::max(first + 1,
          size_t(std::lround(high * kSamples / pcm.rate))));
      float power = 0;
      for (size_t bin = first; bin < end; ++bin)
        power = std::max(power, real_[bin] * real_[bin] + imag_[bin] * imag_[bin]);
      const float amplitude = std::sqrt(power) * (4.f / kSamples);
      // Fixed noise floor: digital silence stays still. The visualization is
      // tapped before master volume, so lowering volume does not erase detail.
      result[band] = std::clamp((20.f * std::log10(std::max(amplitude, 1e-7f)) + 60.f) / 60.f, 0.f, 1.f);
    }
    return result;
  }
 private:
  std::array<float, kSamples> real_{}, imag_{}, window_{};
  bool window_ready_ = false;
};

// Millisecond-based motion, independent of redraw rate; unsigned subtraction
// supports device-clock wrap. Pauses settle completely rather than repainting.
class Motion {
 public:
  bool update(uint32_t now, bool playing, const Bands &target) {
    if (!initialized_) { last_ = now; initialized_ = true; }
    const float dt = std::min(uint32_t(now - last_), uint32_t(100)) / 1000.f;
    last_ = now;
    speed_ += ((playing ? 1.f : 0.f) - speed_) * (1.f - std::exp(-dt / .25f));
    if (!playing && speed_ < .002f) speed_ = 0;
    const auto previous = angle_;
    angle_ = std::fmod(angle_ + speed_ * dt * .19f, 6.283185307f);
    bool changed = angle_ != previous;
    for (size_t i = 0; i < kBands; ++i) {
      const float goal = playing ? target[i] : 0;
      const auto old = levels_[i];
      levels_[i] += (goal - old) * (1.f - std::exp(-dt / (goal > old ? .045f : .23f)));
      if (goal == 0 && levels_[i] < .002f) levels_[i] = 0;
      changed |= std::lround(old * 10) != std::lround(levels_[i] * 10);
    }
    return changed;
  }
  float angle() const { return angle_; }
  const Bands &levels() const { return levels_; }
 private:
  bool initialized_ = false;
  uint32_t last_ = 0;
  float angle_ = 0, speed_ = 0;
  Bands levels_{};
};
} // namespace smart_speaker::music
