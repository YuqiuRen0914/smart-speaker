#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LUNAROS_GESTURE_AXIS_UNDECIDED = 0,
    LUNAROS_GESTURE_AXIS_HORIZONTAL,
    LUNAROS_GESTURE_AXIS_VERTICAL,
} lunaros_gesture_axis_t;

typedef enum {
    LUNAROS_INTERACTION_SURFACE_OTHER = 0,
    LUNAROS_INTERACTION_SURFACE_AMBIENT,
    LUNAROS_INTERACTION_SURFACE_LAUNCHER,
    LUNAROS_INTERACTION_SURFACE_CONTROLS,
} lunaros_interaction_surface_t;

typedef enum {
    LUNAROS_GESTURE_ACTION_NONE = 0,
    LUNAROS_GESTURE_ACTION_TAP,
    LUNAROS_GESTURE_ACTION_OPEN_LAUNCHER,
    LUNAROS_GESTURE_ACTION_OPEN_CONTROLS,
    LUNAROS_GESTURE_ACTION_OPEN_AMBIENT,
} lunaros_gesture_action_t;

typedef struct {
    int16_t start_x;
    int16_t start_y;
    int16_t last_x;
    int16_t last_y;
    lunaros_gesture_axis_t axis;
    bool active;
    bool committed;
} lunaros_interaction_gesture_t;

int lunaros_wifi_quality_from_rssi(bool connected, int32_t rssi);

void lunaros_interaction_gesture_begin(lunaros_interaction_gesture_t *gesture,
                                       int16_t x, int16_t y);

lunaros_gesture_action_t lunaros_interaction_route_vertical(
    lunaros_interaction_surface_t surface, int16_t delta_y);

lunaros_gesture_action_t lunaros_interaction_gesture_update(
    lunaros_interaction_gesture_t *gesture,
    lunaros_interaction_surface_t surface, int16_t x, int16_t y,
    bool released);

void lunaros_interaction_gesture_cancel(
    lunaros_interaction_gesture_t *gesture);

#ifdef __cplusplus
}
#endif
