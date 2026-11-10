#pragma once
#include "display/backend.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
namespace lunaredu::display {
// Absolute frame deadlines. Missed slots are skipped, so a delayed screen
// cannot build a queue of obsolete animation frames.
class ScreenFramePacer {
 public:
  bool configure(unsigned target_fps);
  void reset() { next_us_ = 0; missed_ = 0; }
  bool due(uint64_t now_us);
  uint64_t next_us() const { return next_us_; }
  uint64_t missed() const { return missed_; }
  uint64_t period_us() const { return period_us_; }

 private:
  uint64_t period_us_ = 0;
  uint64_t next_us_ = 0;
  uint64_t missed_ = 0;
};

}
