#pragma once
#include <chrono>
#include <cstdint>

namespace lunaredu::lunaros {
inline int64_t ota_verification_now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
}
// Failed observations never grant admission. Transient SDK/resource failures
// may be re-observed without a reboot, at most once every five seconds.
struct OtaVerificationRetry {
  static constexpr int64_t interval_ms = 5000;
  bool transient = false;
  int64_t next_ms = 0;
  void record(bool retryable, int64_t now_ms) {
    transient = retryable;
    next_ms = now_ms + interval_ms;
  }
  bool due(int64_t now_ms) const { return transient && now_ms >= next_ms; }
};
} // namespace lunaredu::lunaros
