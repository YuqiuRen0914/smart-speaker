#pragma once

#include "smart_speaker_sensors.h"

namespace smart_speaker {

enum class TouchContext : uint8_t { Home, Launcher, Secondary, Menu, Feature, Music, Blocked };
enum class TouchRoute : uint8_t { None, OpenProductShell, MenuConfirm, Previous, Next, PreviousTrack, NextTrack };

// Gesture meaning follows the owning page; horizontal motion never goes back.
TouchRoute route_touch_gesture(TouchGesture gesture, TouchContext context);

} // namespace smart_speaker
