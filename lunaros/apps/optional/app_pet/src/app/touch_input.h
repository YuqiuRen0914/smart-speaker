#pragma once

#include "lunaredu_pet_render.h"

namespace lunaredu::pet {

class TouchInput {
 public:
  void reset();
  bool handle(const TouchEvent &event, TouchResult &result);

 private:
  bool tracking_ = false;
  bool captured_ = false;
  uint16_t start_x_ = 0, start_y_ = 0;
  uint16_t last_x_ = 0, last_y_ = 0;
  uint16_t width_ = 0, height_ = 0;
  uint32_t started_ms_ = 0;
  float path_px_ = 0;
};

} // namespace lunaredu::pet
