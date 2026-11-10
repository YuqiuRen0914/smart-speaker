#pragma once
#include "lunaros_ui_interaction_layout.generated.h"
#include <cmath>
#include <cstdint>

namespace lunaredu::desktop {
// Logical display coordinates. Adapters own contact freshness, cancellation,
// rotation and safety gates; release coordinates may be cleared by hardware.
inline bool is_home_swipe(float start_x,float start_y,float last_x,float last_y,uint32_t duration_ms){
  namespace l=home_gesture_layout;
  return duration_ms<=l::maximumDurationMs && start_x>=0 && start_x<l::screenSize &&
      start_y>=l::screenSize-l::edgeHeight && start_y<l::screenSize &&
      start_y-last_y>=l::travel && std::abs(last_x-start_x)*l::directionRatio<=start_y-last_y;
}
}
