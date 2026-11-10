#include "lunaredu_lunaros/desktop/core/interaction_policy.h"

#include <cassert>
#include <cstring>
#include <iostream>

namespace {

lunaros_gesture_action_t update(lunaros_interaction_gesture_t &gesture,
                                lunaros_interaction_surface_t surface,
                                int x, int y, bool released = false) {
  return lunaros_interaction_gesture_update(
      &gesture, surface, static_cast<int16_t>(x), static_cast<int16_t>(y),
      released);
}

void verify_ambient_vertical_routes() {
  assert(lunaros_interaction_route_vertical(
             LUNAROS_INTERACTION_SURFACE_AMBIENT, -1) ==
         LUNAROS_GESTURE_ACTION_OPEN_LAUNCHER);
  assert(lunaros_interaction_route_vertical(
             LUNAROS_INTERACTION_SURFACE_AMBIENT, 1) ==
         LUNAROS_GESTURE_ACTION_OPEN_CONTROLS);

  lunaros_interaction_gesture_t gesture{};
  lunaros_interaction_gesture_begin(&gesture, 100, 180);
  assert(update(gesture, LUNAROS_INTERACTION_SURFACE_AMBIENT, 102, 168) ==
         LUNAROS_GESTURE_ACTION_NONE);
  assert(gesture.axis == LUNAROS_GESTURE_AXIS_VERTICAL);
  assert(update(gesture, LUNAROS_INTERACTION_SURFACE_AMBIENT, 104, 150) ==
         LUNAROS_GESTURE_ACTION_OPEN_LAUNCHER);

  lunaros_interaction_gesture_begin(&gesture, 100, 100);
  assert(update(gesture, LUNAROS_INTERACTION_SURFACE_AMBIENT, 98, 130) ==
         LUNAROS_GESTURE_ACTION_OPEN_CONTROLS);
}

void verify_internal_surfaces_ignore_drag() {
  lunaros_interaction_gesture_t gesture{};
  lunaros_interaction_gesture_begin(&gesture, 220, 160);
  assert(update(gesture, LUNAROS_INTERACTION_SURFACE_LAUNCHER, 196, 164) ==
         LUNAROS_GESTURE_ACTION_NONE);
  assert(gesture.axis == LUNAROS_GESTURE_AXIS_HORIZONTAL);
  assert(update(gesture, LUNAROS_INTERACTION_SURFACE_LAUNCHER, 150, 195) ==
         LUNAROS_GESTURE_ACTION_NONE);
  assert(update(gesture, LUNAROS_INTERACTION_SURFACE_LAUNCHER, 150, 195,
                true) == LUNAROS_GESTURE_ACTION_NONE);
  assert(lunaros_interaction_route_vertical(
             LUNAROS_INTERACTION_SURFACE_LAUNCHER, -1) ==
         LUNAROS_GESTURE_ACTION_NONE);
  assert(lunaros_interaction_route_vertical(
             LUNAROS_INTERACTION_SURFACE_LAUNCHER, 1) ==
         LUNAROS_GESTURE_ACTION_NONE);
  assert(lunaros_interaction_route_vertical(
             LUNAROS_INTERACTION_SURFACE_CONTROLS, 1) ==
         LUNAROS_GESTURE_ACTION_NONE);
  assert(lunaros_interaction_route_vertical(
             LUNAROS_INTERACTION_SURFACE_CONTROLS, -1) ==
         LUNAROS_GESTURE_ACTION_NONE);
  assert(lunaros_interaction_route_vertical(
             LUNAROS_INTERACTION_SURFACE_OTHER, -1) ==
         LUNAROS_GESTURE_ACTION_NONE);

  lunaros_interaction_gesture_begin(&gesture, 200, 100);
  assert(update(gesture, LUNAROS_INTERACTION_SURFACE_LAUNCHER, 200, 132) ==
         LUNAROS_GESTURE_ACTION_NONE);

  lunaros_interaction_gesture_begin(&gesture, 200, 180);
  assert(update(gesture, LUNAROS_INTERACTION_SURFACE_CONTROLS, 200, 148) ==
         LUNAROS_GESTURE_ACTION_NONE);

  lunaros_interaction_gesture_begin(&gesture, 200, 180);
  assert(update(gesture, LUNAROS_INTERACTION_SURFACE_LAUNCHER, 200, 148) ==
         LUNAROS_GESTURE_ACTION_NONE);
}

void verify_tap_requires_no_locked_axis() {
  lunaros_interaction_gesture_t gesture{};
  lunaros_interaction_gesture_begin(&gesture, 100, 100);
  assert(update(gesture, LUNAROS_INTERACTION_SURFACE_AMBIENT, 106, 107, true) ==
         LUNAROS_GESTURE_ACTION_TAP);

  lunaros_interaction_gesture_begin(&gesture, 100, 100);
  assert(update(gesture, LUNAROS_INTERACTION_SURFACE_AMBIENT, 120, 102) ==
         LUNAROS_GESTURE_ACTION_NONE);
  assert(update(gesture, LUNAROS_INTERACTION_SURFACE_AMBIENT, 106, 104, true) ==
         LUNAROS_GESTURE_ACTION_NONE);
}









void verify_wifi_rssi_quality_boundaries() {
  assert(lunaros_wifi_quality_from_rssi(false, -40) == 0);
  assert(lunaros_wifi_quality_from_rssi(true, 0) == 1);
  assert(lunaros_wifi_quality_from_rssi(true, -90) == 1);
  assert(lunaros_wifi_quality_from_rssi(true, -76) == 1);
  assert(lunaros_wifi_quality_from_rssi(true, -75) == 2);
  assert(lunaros_wifi_quality_from_rssi(true, -68) == 2);
  assert(lunaros_wifi_quality_from_rssi(true, -67) == 3);
  assert(lunaros_wifi_quality_from_rssi(true, -56) == 3);
  assert(lunaros_wifi_quality_from_rssi(true, -55) == 4);
  assert(lunaros_wifi_quality_from_rssi(true, -30) == 4);
}

} // namespace

int main() {
  verify_ambient_vertical_routes();
  verify_internal_surfaces_ignore_drag();
  verify_tap_requires_no_locked_axis();
  verify_wifi_rssi_quality_boundaries();
  std::cout << "interaction_policy_test passed\n";
  return 0;
}
