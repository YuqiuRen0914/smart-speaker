#pragma once

#include <algorithm>
#include <cstdint>

namespace lunaredu::lunaros::platform_link {

inline uint32_t pairing_rotation_delay_ms(int64_t expires_at_ms,
                                          int64_t wall_now_ms) {
  constexpr int64_t kLeadMs = 30'000;
  constexpr int64_t kMaximumDelayMs = 270'000;
  return static_cast<uint32_t>(std::clamp(
      expires_at_ms - wall_now_ms - kLeadMs, int64_t{0}, kMaximumDelayMs));
}

inline float pairing_rotation_remaining_fraction(uint32_t now_ms,
                                                 uint32_t due_ms,
                                                 uint32_t duration_ms) {
  if (duration_ms == 0) return -1.0F;
  const int32_t remaining = static_cast<int32_t>(due_ms - now_ms);
  if (remaining <= 0) return 0.0F;
  if (static_cast<uint32_t>(remaining) >= duration_ms) return 1.0F;
  return static_cast<float>(remaining) / static_cast<float>(duration_ms);
}

class PairingCountdown {
public:
  bool needs_observation(bool active, int64_t expires_at_ms) const {
    return !active || expires_at_ms != expires_at_ms_;
  }
  void observe(bool active, int64_t expires_at_ms, int64_t wall_now_ms, uint32_t now_ms) {
    if (!active || expires_at_ms <= 0 || wall_now_ms <= 0) {
      expires_at_ms_ = 0;
      duration_ms_ = due_ms_ = 0;
      return;
    }
    if (expires_at_ms == expires_at_ms_) return;
    expires_at_ms_ = expires_at_ms;
    duration_ms_ = static_cast<uint32_t>(std::clamp<int64_t>(
        expires_at_ms - std::max<int64_t>(wall_now_ms, 0), 0, INT32_MAX));
    due_ms_ = now_ms + duration_ms_;
  }
  float remaining_fraction(uint32_t now_ms) const {
    if (!expires_at_ms_) return -1.0f;
    return duration_ms_ ? pairing_rotation_remaining_fraction(now_ms, due_ms_, duration_ms_) : 0.0f;
  }
private:
  int64_t expires_at_ms_ = 0;
  uint32_t duration_ms_ = 0, due_ms_ = 0;
};

} // namespace lunaredu::lunaros::platform_link
