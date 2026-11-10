#pragma once
#include <atomic>

namespace lunaredu::micropython {
// Failed attempts release the gate. Once a task exists, even a readiness timeout
// must retain it: the owner may still complete startup and must not be duplicated.
class StartAttempt {
  std::atomic<bool> &gate_;
  bool acquired_;
  bool committed_ = false;
public:
  explicit StartAttempt(std::atomic<bool> &gate) : gate_(gate), acquired_(false) {
    bool expected = false;
    acquired_ = gate_.compare_exchange_strong(expected, true);
  }
  ~StartAttempt() {
    if (acquired_ && !committed_) gate_.store(false, std::memory_order_release);
  }
  explicit operator bool() const { return acquired_; }
  void commit() { committed_ = true; }
  StartAttempt(const StartAttempt &) = delete;
  StartAttempt &operator=(const StartAttempt &) = delete;
};
} // namespace lunaredu::micropython
