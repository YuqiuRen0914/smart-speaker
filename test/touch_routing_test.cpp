#include "smart_speaker_touch_routing.h"

#include <cassert>

using smart_speaker::TouchGesture;
using smart_speaker::TouchRoute;
using smart_speaker::route_touch_gesture;

int main() {
  assert(route_touch_gesture(TouchGesture::SwipeUp, false) ==
         TouchRoute::OpenSettings);
  assert(route_touch_gesture(TouchGesture::SwipeDown, false) ==
         TouchRoute::None);
  assert(route_touch_gesture(TouchGesture::SwipeUp, true) == TouchRoute::None);
  assert(route_touch_gesture(TouchGesture::SwipeDown, true) == TouchRoute::None);
  assert(route_touch_gesture(TouchGesture::SwipeLeft, false) ==
         TouchRoute::NextTrack);
  assert(route_touch_gesture(TouchGesture::SwipeRight, false) ==
         TouchRoute::PreviousTrack);
  assert(route_touch_gesture(TouchGesture::SwipeLeft, true) ==
         TouchRoute::MenuBack);
  assert(route_touch_gesture(TouchGesture::SwipeRight, true) ==
         TouchRoute::MenuBack);
  assert(route_touch_gesture(TouchGesture::Tap, true) == TouchRoute::MenuConfirm);
  assert(route_touch_gesture(TouchGesture::Tap, false) ==
         TouchRoute::TogglePlayback);
}
