#pragma once

#include <algorithm>
#include <cstdlib>
#include "smart_speaker_sensors.h"

namespace smart_speaker {

// The touch glass is mounted 180 degrees relative to the LCD address
// space. Publish screen coordinates for both hit testing and navigation.
constexpr uint16_t screen_touch_axis(uint16_t raw) {
  return 239 - std::min<uint16_t>(raw, 239);
}

struct PetTouchTarget {
  unsigned x = 0, y = 0, width = 0, height = 0;
  uint32_t packed() const { return x | (y << 8) | (width << 16) | (height << 24); }
  static PetTouchTarget unpack(uint32_t n) {
    return {n & 255U, (n >> 8) & 255U, (n >> 16) & 255U, n >> 24};
  }
  bool contains(unsigned px, unsigned py) const {
    return px >= x && px < x + width && py >= y && py < y + height;
  }
};

// One owner samples the panel. The display sees only a bounded current drag
// and one-shot counters, so a late display frame cannot grow an input queue.
class TouchInput {
 public:
  void cancel() {
    active_ = pet_ = false;
    visual_.dragging = false;
    visual_.dx = visual_.dy = 0;
    last_tap_valid_ = false;
    suppress_ = true; // A held finger must lift before the new page owns it.
  }

  TouchGesture update(uint32_t now, bool pressed, unsigned x, unsigned y,
                      PetTouchTarget target, bool valid = true) {
    if (!valid || (active_ && pet_ && !target.width)) {
      cancel();
      return TouchGesture::None;
    }
    if (suppress_) {
      if (!pressed) suppress_ = false;
      return TouchGesture::None;
    }
    if (pressed && !active_) {
      active_ = true;
      start_ms_ = now;
      start_x_ = last_x_ = static_cast<int>(x);
      start_y_ = last_y_ = static_cast<int>(y);
      pet_ = target.contains(x, y);
      sling_ = pet_ && last_tap_valid_ && now - last_tap_ms_ <= 300 &&
          std::abs(start_x_ - tap_x_) <= 26 && std::abs(start_y_ - tap_y_) <= 26;
      last_tap_valid_ = false;
      velocity_x_ = velocity_y_ = 0;
      move_samples_ = 0;
      last_motion_ms_ = now;
      moved_ = false;
      visual_.dragging = false;
      visual_.dx = visual_.dy = 0;
      return TouchGesture::None;
    }
    if (!active_) return TouchGesture::None;
    if (pressed) {
      const int mx = int(x) - last_x_, my = int(y) - last_y_;
      if (mx || my) {
        const auto dt = std::clamp<uint32_t>(now - last_motion_ms_, 12, 100);
        velocity_x_ = (velocity_x_ + std::clamp(mx * 1000 / int(dt), -550, 550)) / 2;
        velocity_y_ = (velocity_y_ + std::clamp(my * 1000 / int(dt), -550, 550)) / 2;
        if (move_samples_ < 3) ++move_samples_;
        last_motion_ms_ = now;
      }
      last_x_ = static_cast<int>(x);
      last_y_ = static_cast<int>(y);
    }
    const int dx = last_x_ - start_x_, dy = last_y_ - start_y_;
    const int ax = std::abs(dx), ay = std::abs(dy);
    const uint32_t elapsed = now - start_ms_;
    if (pet_) {
      moved_ = moved_ || ax >= 12 || ay >= 12;
      if (moved_) {
        visual_.dragging = true;
        visual_.dx = static_cast<int16_t>(dx);
        visual_.dy = static_cast<int16_t>(dy);
      }
    }
    if (pressed) return TouchGesture::None;
    active_ = false;
    if (pet_) {
      if (visual_.dragging) {
        visual_.dragging = false;
        visual_.release_dx = visual_.dx;
        visual_.release_dy = visual_.dy;
        const bool recent_motion = move_samples_ >= 2 && now - last_motion_ms_ <= 70;
        visual_.release_vx = recent_motion && std::abs(velocity_x_) >= 100 ? velocity_x_ : 0;
        visual_.release_vy = recent_motion && std::abs(velocity_y_) >= 100 ? velocity_y_ : 0;
        visual_.release_sling = sling_;
        ++visual_.releases;
      } else if (!moved_ && elapsed <= 450) {
        ++visual_.taps;
        last_tap_valid_ = true;
        last_tap_ms_ = now;
        tap_x_ = start_x_; tap_y_ = start_y_;
      }
      return TouchGesture::None;
    }
    if (ax >= 45 && ax > ay) return dx > 0 ? TouchGesture::SwipeRight : TouchGesture::SwipeLeft;
    if (ay >= 45) return dy > 0 ? TouchGesture::SwipeDown : TouchGesture::SwipeUp;
    return elapsed <= 450 ? TouchGesture::Tap : TouchGesture::None;
  }

  PetTouchFeedback feedback() const { return visual_; }

 private:
  PetTouchFeedback visual_{};
  uint32_t start_ms_ = 0;
  uint32_t last_tap_ms_ = 0, last_motion_ms_ = 0;
  int start_x_ = 0, start_y_ = 0, last_x_ = 0, last_y_ = 0;
  int tap_x_ = 0, tap_y_ = 0, velocity_x_ = 0, velocity_y_ = 0;
  uint8_t move_samples_ = 0;
  bool active_ = false, pet_ = false, moved_ = false;
  bool suppress_ = false;
  bool sling_ = false, last_tap_valid_ = false;
};

} // namespace smart_speaker
