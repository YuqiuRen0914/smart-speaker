#pragma once
#include <atomic>
#include <cstdint>

namespace lunaredu::lunaros::bluetooth_service_internal {
// An accepted SDK deinit request is not a completed protocol teardown. Keep
// ownership until its callback; retries only resubmit failed/unqueued stages.
class ProfileRetirement {
  std::atomic<uint32_t> owned_{0}, closing_{0}, failed_{0};
public:
  void acquired(uint32_t profile) { owned_.fetch_or(profile); }
  bool begin_close(uint32_t profile) {
    if (!(owned_.load() & profile)) return false;
    return !(closing_.fetch_or(profile) & profile);
  }
  void completed(uint32_t profile) {
    owned_.fetch_and(~profile); closing_.fetch_and(~profile);
  }
  void failed(uint32_t profile) {
    closing_.fetch_and(~profile); failed_.fetch_or(profile);
  }
  bool take_failure() { return failed_.exchange(0) != 0; }
  bool pending() const { return owned_.load() != 0; }
};
} // namespace lunaredu::lunaros::bluetooth_service_internal
