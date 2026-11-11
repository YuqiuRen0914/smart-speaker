#pragma once

#include <cstdint>
#include <cmath>

namespace smart_speaker {

// A single shock or slow tilt is not a shake. Look for two opposing dynamic
// acceleration peaks from distinct sensor samples, then let the display settle.
class PetShake {
 public:
  void sample(uint32_t seq, uint32_t ms, float x, float y, float z) {
    if (!initialized_ || ms - last_ms_ > 250) {
      initialized_ = true;
      gx_ = x; gy_ = y; gz_ = z;
      armed_ = false;
      seq_ = seq;
      last_ms_ = ms;
      return;
    }
    if (seq == seq_) return;
    seq_ = seq;
    last_ms_ = ms;
    const float ax = x - gx_, ay = y - gy_, az = z - gz_;
    gx_ += ax * .12f; gy_ += ay * .12f; gz_ += az * .12f;
    if (count && ms - fired_ms_ < 850) return;
    if (armed_ && ms - peak_ms_ > 220) armed_ = false;
    if (ax * ax + ay * ay + az * az < .60f * .60f) return;
    if (armed_ && ax * px_ + ay * py_ + az * pz_ < -.24f) {
      direction_x = std::abs(ax) > .28f ? (ax > 0 ? 1 : -1) : 0;
      direction_y = std::abs(ay) > .28f ? (ay > 0 ? 1 : -1) : 0;
      if (!direction_x && !direction_y) direction_y = -1;
      ++count;
      fired_ms_ = ms;
      armed_ = false;
    } else if (!armed_) {
      px_ = ax; py_ = ay; pz_ = az;
      peak_ms_ = ms;
      armed_ = true;
    }
  }

  uint32_t count = 0;
  int8_t direction_x = 0, direction_y = 0;

 private:
  float gx_ = 0, gy_ = 0, gz_ = 0, px_ = 0, py_ = 0, pz_ = 0;
  uint32_t seq_ = 0, last_ms_ = 0, peak_ms_ = 0, fired_ms_ = 0;
  bool initialized_ = false, armed_ = false;
};

} // namespace smart_speaker
