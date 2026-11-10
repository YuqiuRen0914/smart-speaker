#include "smart_speaker_touch_routing.h"

namespace smart_speaker {

TouchRoute route_touch_gesture(TouchGesture gesture, bool menu_active) {
  if (menu_active) {
    switch (gesture) {
      case TouchGesture::Tap:
        return TouchRoute::MenuConfirm;
      case TouchGesture::SwipeLeft:
      case TouchGesture::SwipeRight:
        return TouchRoute::MenuBack;
      default:
        return TouchRoute::None;
    }
  }
  switch (gesture) {
    case TouchGesture::Tap:
      return TouchRoute::TogglePlayback;
    case TouchGesture::LongPress:
      return TouchRoute::StartVoice;
    case TouchGesture::LongPressRelease:
      return TouchRoute::FinishVoice;
    case TouchGesture::SwipeLeft:
      return TouchRoute::NextTrack;
    case TouchGesture::SwipeRight:
      return TouchRoute::PreviousTrack;
    case TouchGesture::SwipeUp:
      return TouchRoute::OpenSettings;
    case TouchGesture::SwipeDown:
    case TouchGesture::None:
    default:
      return TouchRoute::None;
  }
}

} // namespace smart_speaker
