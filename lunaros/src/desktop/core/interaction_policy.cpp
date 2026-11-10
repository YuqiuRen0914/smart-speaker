#include "lunaredu_lunaros/desktop/core/interaction_policy.h"

#include <cstdlib>

namespace {

constexpr int kAxisLockDistancePx = 10;
constexpr int kAxisDominancePx = 4;
constexpr int kCommitDistancePx = 24;
constexpr int kTapDistancePx = 12;

} // namespace

extern "C" int lunaros_wifi_quality_from_rssi(bool connected, int32_t rssi) {
  if (!connected) return 0;
  // A connected station can briefly report 0 before its first AP sample.
  // Show the weakest connected state instead of claiming perfect reception.
  if (rssi >= 0 || rssi < -127) return 1;
  if (rssi >= -55) return 4;
  if (rssi >= -67) return 3;
  if (rssi >= -75) return 2;
  return 1;
}

extern "C" void lunaros_interaction_gesture_begin(
    lunaros_interaction_gesture_t *gesture, int16_t x, int16_t y) {
  if (gesture == nullptr) return;
  *gesture = {};
  gesture->start_x = x;
  gesture->start_y = y;
  gesture->last_x = x;
  gesture->last_y = y;
  gesture->active = true;
}

extern "C" lunaros_gesture_action_t lunaros_interaction_route_vertical(
    lunaros_interaction_surface_t surface, int16_t delta_y) {
  if (delta_y == 0) return LUNAROS_GESTURE_ACTION_NONE;
  if (surface == LUNAROS_INTERACTION_SURFACE_AMBIENT) {
    return delta_y < 0 ? LUNAROS_GESTURE_ACTION_OPEN_LAUNCHER
                       : LUNAROS_GESTURE_ACTION_OPEN_CONTROLS;
  }
  return LUNAROS_GESTURE_ACTION_NONE;
}

extern "C" lunaros_gesture_action_t lunaros_interaction_gesture_update(
    lunaros_interaction_gesture_t *gesture,
    lunaros_interaction_surface_t surface, int16_t x, int16_t y,
    bool released) {
  if (gesture == nullptr || !gesture->active || gesture->committed) {
    return LUNAROS_GESTURE_ACTION_NONE;
  }

  gesture->last_x = x;
  gesture->last_y = y;
  const int delta_x = static_cast<int>(x) - gesture->start_x;
  const int delta_y = static_cast<int>(y) - gesture->start_y;
  const int distance_x = std::abs(delta_x);
  const int distance_y = std::abs(delta_y);

  if (gesture->axis == LUNAROS_GESTURE_AXIS_UNDECIDED) {
    if (distance_x >= kAxisLockDistancePx &&
        distance_x > distance_y + kAxisDominancePx) {
      gesture->axis = LUNAROS_GESTURE_AXIS_HORIZONTAL;
    } else if (distance_y >= kAxisLockDistancePx &&
               distance_y > distance_x + kAxisDominancePx) {
      gesture->axis = LUNAROS_GESTURE_AXIS_VERTICAL;
    }
  }

  if (gesture->axis == LUNAROS_GESTURE_AXIS_VERTICAL &&
      distance_y >= kCommitDistancePx) {
    const lunaros_gesture_action_t action =
        lunaros_interaction_route_vertical(surface,
            static_cast<int16_t>(delta_y));
    if (action != LUNAROS_GESTURE_ACTION_NONE) {
      gesture->committed = true;
      gesture->active = false;
      return action;
    }
  }

  if (!released) return LUNAROS_GESTURE_ACTION_NONE;

  gesture->active = false;
  if (gesture->axis == LUNAROS_GESTURE_AXIS_UNDECIDED &&
      distance_x <= kTapDistancePx && distance_y <= kTapDistancePx) {
    return LUNAROS_GESTURE_ACTION_TAP;
  }
  return LUNAROS_GESTURE_ACTION_NONE;
}

extern "C" void lunaros_interaction_gesture_cancel(
    lunaros_interaction_gesture_t *gesture) {
  if (gesture == nullptr) return;
  gesture->active = false;
  gesture->committed = false;
  gesture->axis = LUNAROS_GESTURE_AXIS_UNDECIDED;
}
