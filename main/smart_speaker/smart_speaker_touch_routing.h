#pragma once

#include "smart_speaker_sensors.h"

namespace smart_speaker {

// Side-effect-free touch routing keeps gesture precedence testable. Menu
// actions themselves remain owned by the runtime/menu state machine.
enum class TouchRoute : uint8_t {
  None,
  TogglePlayback,
  StartVoice,
  FinishVoice,
  NextTrack,
  PreviousTrack,
  OpenSettings,
  MenuConfirm,
  MenuBack,
};

TouchRoute route_touch_gesture(TouchGesture gesture, bool menu_active);

} // namespace smart_speaker
