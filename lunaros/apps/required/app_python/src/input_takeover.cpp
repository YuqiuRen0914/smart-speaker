#include "lunaros_python_input_takeover.h"

namespace lunaredu::python {
bool InputTakeover::enter(std::string &error) {
  error.clear();
  if (phase_ == Phase::Foreground) return true;
  if (phase_ != Phase::Idle) { error = "python_input_cleanup_pending"; return false; }
  if (port_.updating()) { error = "firmware_update_active"; return false; }
  // Withdraw authority before the first callback, including timeout failures
  // that may have already changed a sampling owner's requested policy.
  port_.block_controls(true);
  phase_ = Phase::PausingAxes;
  if (!port_.pause_axes(error)) return false;
  phase_ = Phase::StoppingImu;
  if (!port_.stop_imu(error)) return false;
  // Programming withdraws remote-control authority; desktop touch remains live.
  phase_ = Phase::ReleasingControls;
  if (!port_.release_controls(error)) return false;
  phase_ = Phase::EnteringScreen;
  if (!port_.enter_screen(error)) return false;
  screen_entered_ = true;
  port_.foreground(true);
  phase_ = Phase::Foreground;
  error.clear();
  return true;
}
bool InputTakeover::leave(std::string &error) {
  error.clear();
  if (phase_ == Phase::Idle) return true;
  phase_ = Phase::Restoring;
  if (screen_entered_) {
    if (!port_.leave_screen(error)) return false;
    screen_entered_ = false;
  }
  // The VM is already retired by SystemApplication. Discard pre-takeover input
  // and restore sampling policies before dropping the control gate. Normal
  // foreground demand decides whether IMU acquisition should resume.
  port_.restore_inputs();
  port_.foreground(false);
  phase_ = Phase::Idle;
  port_.block_controls(false);
  return true;
}
} // namespace lunaredu::python
