#pragma once
#include "desktop/ui/touch_input.h"
#include "desktop/graphics/canvas_spec.h"
#include "desktop/navigation/home_gesture.h"
#include <algorithm>
#include <cstdlib>

namespace lunaredu::desktop {

enum class TouchGesture : uint8_t { None, Tap, Previous, Next, Home, PullDown };
struct TouchEvent {
  TouchGesture gesture = TouchGesture::None;
  uint16_t x = 0;
  uint16_t y = 0;
};

class TouchInputRouter {
 public:
  struct Config {
    uint16_t width = canvas_layout::side, height = canvas_layout::side;
    uint32_t maximum_sample_age_ms = 120;
    uint16_t pull_start_y = 30, pull_end_y = 190;
  };
  TouchInputRouter() = default;
  explicit TouchInputRouter(Config config) : config_(config) {}
  void cancel() { armed_ = false; tracking_ = false; }

  TouchEvent update(const TouchInput &input, int64_t now_us, bool pull_refresh=false) {
    return update_impl(input,now_us,pull_refresh,{home_gesture_layout::screenSize,home_gesture_layout::edgeHeight,home_gesture_layout::travel,home_gesture_layout::directionRatio,home_gesture_layout::maximumDurationMs});
  }

  bool tracking() const { return tracking_; }
  int pull_refresh_distance(int64_t now_us) const;

 private:
  struct Geometry {int side,edge,travel,ratio;uint32_t maximum_duration;};
  TouchEvent update_impl(const TouchInput &,int64_t,bool,const Geometry &);
  Config config_{};
  bool armed_ = false;
  bool tracking_ = false;
  uint32_t started_ms_ = 0;
  uint16_t start_x_ = 0, start_y_ = 0, last_x_ = 0, last_y_ = 0;
  int max_travel_ = 0;
};

}  // namespace lunaredu::desktop
