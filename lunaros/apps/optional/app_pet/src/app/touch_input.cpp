#include "app/touch_input.h"
#include "pet_render_core.h"

#include <algorithm>
#include <cmath>

namespace lunaredu::pet {

void TouchInput::reset() {
  tracking_ = captured_ = false;
  start_x_ = start_y_ = last_x_ = last_y_ = width_ = height_ = 0;
  started_ms_ = 0;
  path_px_ = 0;
  pet_render_touch(0, 0, 0);
}

bool TouchInput::handle(const TouchEvent &event, TouchResult &result) {
  result = {};
  if (event.phase == TouchPhase::Cancel) {
    reset();
    return true;
  }
  if (event.width == 0 || event.height == 0 ||
      event.x >= event.width || event.y >= event.height)
    return false;

  float local_x = 0, local_y = 0;
  const bool hit = pet_render_project_touch(
      event.x, event.y, &local_x, &local_y) != 0;
  if (event.phase == TouchPhase::Down) {
    reset();
    tracking_ = true;
    captured_ = hit;
    start_x_ = last_x_ = event.x;
    start_y_ = last_y_ = event.y;
    width_ = event.width;
    height_ = event.height;
    started_ms_ = event.time_ms;
    if (hit) pet_render_touch(1, local_x, local_y);
    result.captured = hit;
    return true;
  }
  if (!tracking_) return true;
  result.captured = captured_;
  if (event.phase == TouchPhase::Move) {
    const float dx = float(int(event.x) - int(last_x_));
    const float dy = float(int(event.y) - int(last_y_));
    path_px_ += std::sqrt(dx * dx + dy * dy);
    last_x_ = event.x;
    last_y_ = event.y;
    if (captured_) pet_render_touch(1, local_x, local_y);
    return true;
  }
  if (event.phase != TouchPhase::Up) return true;

  const float final_dx = float(int(event.x) - int(last_x_));
  const float final_dy = float(int(event.y) - int(last_y_));
  path_px_ += std::sqrt(final_dx * final_dx + final_dy * final_dy);
  const float displacement_x = float(int(event.x) - int(start_x_));
  const float displacement_y = float(int(event.y) - int(start_y_));
  const float displacement = std::sqrt(displacement_x * displacement_x +
                                       displacement_y * displacement_y);
  const float extent = float(std::min(width_, height_));
  const uint32_t duration = event.time_ms - started_ms_;
  if (captured_) {
    if (duration >= 650U && path_px_ <= extent * .12F) {
      result.gesture = TouchGesture::Hold;
      result.emotion = "loving";
    } else if (duration >= 120U && path_px_ >= extent * .16F) {
      result.gesture = TouchGesture::Stroke;
      result.emotion = "excited";
    } else if (duration <= 450U && displacement <= extent * .04F &&
               path_px_ <= extent * .08F) {
      result.gesture = TouchGesture::Tap;
      result.emotion = "happy";
    }
  }
  reset();
  return true;
}

const char *touch_gesture_name(TouchGesture gesture) {
  switch (gesture) {
  case TouchGesture::Tap: return "tap";
  case TouchGesture::Hold: return "hold";
  case TouchGesture::Stroke: return "stroke";
  case TouchGesture::None: return "none";
  }
  return "none";
}

} // namespace lunaredu::pet
