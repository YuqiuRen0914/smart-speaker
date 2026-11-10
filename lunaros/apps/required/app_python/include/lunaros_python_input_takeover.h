#pragma once
#include <string>

namespace lunaredu::python {
// All callbacks run on the existing main/input owner. A failed acquisition is
// still a claimed transition: AppController must finish checked cleanup before
// restoring the previous application. Screen entry is atomic on failure.
class InputTakeoverPort {
 public:
  virtual ~InputTakeoverPort() = default;
  virtual bool updating() const = 0;
  virtual void block_controls(bool blocked) = 0;
  virtual bool pause_axes(std::string &) = 0;
  virtual bool stop_imu(std::string &) = 0;
  virtual bool release_controls(std::string &) = 0;
  virtual bool enter_screen(std::string &) = 0;
  virtual bool leave_screen(std::string &) = 0;
  virtual void restore_inputs() = 0;
  virtual void foreground(bool active) = 0;
};

class InputTakeover {
 public:
  enum class Phase { Idle, PausingAxes, StoppingImu, ReleasingControls, EnteringScreen, Foreground, Restoring };
  explicit InputTakeover(InputTakeoverPort &port) : port_(port) {}
  Phase phase() const { return phase_; }
  bool transition(bool active, std::string &error) { return active ? enter(error) : leave(error); }
 private:
  bool enter(std::string &error);
  bool leave(std::string &error);
  InputTakeoverPort &port_;
  Phase phase_ = Phase::Idle;
  bool screen_entered_ = false;
};
} // namespace lunaredu::python
