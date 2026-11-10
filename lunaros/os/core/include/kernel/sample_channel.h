#pragma once

#include <cstdint>
#include <mutex>
#include <type_traits>

namespace lunaredu::lunaros {
// Timestamp is completion time in the consumer's monotonic microsecond clock.
// Publish failures as valid=false; do not silently retain a successful reading.
template <typename T> struct SampleSnapshot {
  static_assert(std::is_trivially_copyable_v<T>, "samples must have bounded, owned values");
  T value{};
  int64_t sampled_at_us = 0;
  uint32_t sequence = 0;
  bool valid = false;
};

// Short locked copy avoids torn payload/time/sequence without spinning or
// exposing a pointer into a producer's working buffer. No allocation or task.
template <typename T> class SampleChannel {
 public:
  void publish(const SampleSnapshot<T> &sample) {
    std::lock_guard<std::mutex> lock(mutex_); latest_ = sample;
  }
  SampleSnapshot<T> snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_); return latest_;
  }
 private:
  mutable std::mutex mutex_;
  SampleSnapshot<T> latest_{};
};

enum class SampleUpdate { Invalid, Unchanged, NewSample };
// One cursor per consumer; only distinct complete samples advance its state.
// A clock reset requires invalidate(). Sequence wrap is supported, and
// a newer timestamp distinguishes a producer restarting at the same sequence.
class SampleCursor {
 public:
  explicit SampleCursor(int64_t maximum_age_us) : maximum_age_us_(maximum_age_us) {}
  void set_maximum_age_us(int64_t value) { maximum_age_us_=value; }
  template <typename T>
  SampleUpdate observe(const SampleSnapshot<T> &sample, int64_t now_us) {
    if (!sample.valid || maximum_age_us_ <= 0 || sample.sampled_at_us < 0 ||
        now_us < sample.sampled_at_us || now_us - sample.sampled_at_us >= maximum_age_us_) {
      invalidate(); return SampleUpdate::Invalid;
    }
    if (valid_ && (sample.sampled_at_us < sampled_at_us_ ||
        (sample.sequence == sequence_ && sample.sampled_at_us == sampled_at_us_)))
      return SampleUpdate::Unchanged;
    sampled_at_us_ = sample.sampled_at_us; sequence_ = sample.sequence; valid_ = true;
    return SampleUpdate::NewSample;
  }
  bool fresh(int64_t now_us) const {
    return valid_ && now_us >= sampled_at_us_ && now_us - sampled_at_us_ < maximum_age_us_;
  }
  void invalidate() { valid_ = false; }
 private:
  int64_t maximum_age_us_, sampled_at_us_ = 0;
  uint32_t sequence_ = 0;
  bool valid_ = false;
};
} // namespace lunaredu::lunaros
