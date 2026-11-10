#pragma once
#include <cstdint>

namespace lunaredu::display {
// Shared screen target. Input sampling and service work have their own clocks.
// Preserve the absolute phase through ordinary scheduler jitter. A missed full
// slot rebases on actual progress instead of replaying old frame deadlines.
inline constexpr uint32_t kScreenTargetFps = 30;
class ScreenFrameCadence {
 public:
  static constexpr uint32_t period_us = (1000000u+kScreenTargetFps-1)/kScreenTargetFps;
  uint32_t delay_us(uint64_t now) const {
    return !started_ || now>=next_ ? 0 : uint32_t(next_-now);
  }
  void began(uint64_t now) {
    // Adding a period to every actual wakeup accumulates the RTOS tick rounding
    // and steals frames even when rendering fits comfortably in the budget.
    // Keep the phase for the current slot; never drain a backlog after an overrun.
    if(started_ && now>=next_ && now-next_<period_us)next_+=period_us;
    else next_=now+period_us;
    started_=true;
  }
 private:
  uint64_t next_=0;
  bool started_=false;
};
}
