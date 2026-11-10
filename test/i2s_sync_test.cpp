#include "smart_speaker_i2s_sync.h"

#include <cassert>
#include <cstdint>
#include <iostream>

using smart_speaker::I2sCompletionFault;
using smart_speaker::I2sCompletionMonitor;

namespace {

void test_large_counter_offset_is_not_a_sync_fault_when_both_advance() {
  I2sCompletionMonitor monitor;
  monitor.reset(10000, 20000, 0);

  assert(monitor.observe(12560, 22560, 50, true) ==
         I2sCompletionFault::kNone);
  assert(monitor.observe(15120, 25120, 100, true) ==
         I2sCompletionFault::kNone);
}

void test_one_sided_stall_requires_two_observation_windows() {
  I2sCompletionMonitor monitor;
  monitor.reset(1000, 1000, 0);

  assert(monitor.observe(1000, 2024, 50, true) ==
         I2sCompletionFault::kNone);
  assert(monitor.observe(1000, 3048, 100, true) ==
         I2sCompletionFault::kLeftStalled);
}

void test_both_idle_and_inactive_periods_are_not_sync_faults() {
  I2sCompletionMonitor monitor;
  monitor.reset(1000, 1000, 0);

  assert(monitor.observe(1000, 1000, 100, true) ==
         I2sCompletionFault::kNone);
  assert(monitor.observe(1000, 5000, 150, false) ==
         I2sCompletionFault::kNone);
  assert(monitor.observe(1000, 6024, 200, true) ==
         I2sCompletionFault::kNone);
}

void test_right_stall_is_detected_symmetrically() {
  I2sCompletionMonitor monitor;
  monitor.reset(500, 500, 0);

  assert(monitor.observe(1524, 500, 50, true) ==
         I2sCompletionFault::kNone);
  assert(monitor.observe(2548, 500, 100, true) ==
         I2sCompletionFault::kRightStalled);
}

}  // namespace

int main() {
  test_large_counter_offset_is_not_a_sync_fault_when_both_advance();
  test_one_sided_stall_requires_two_observation_windows();
  test_both_idle_and_inactive_periods_are_not_sync_faults();
  test_right_stall_is_detected_symmetrically();
  std::cout << "i2s sync tests passed\n";
  return 0;
}
