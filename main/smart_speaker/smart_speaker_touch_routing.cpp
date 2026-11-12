#include "smart_speaker_touch_routing.h"

namespace smart_speaker {

TouchRoute route_touch_gesture(TouchGesture gesture, TouchContext context) {
  if (context == TouchContext::Blocked) return TouchRoute::None;
  if (context == TouchContext::Home)
    return gesture == TouchGesture::SwipeLeft ? TouchRoute::OpenProductShell : TouchRoute::None;
  if (gesture == TouchGesture::Tap) return TouchRoute::MenuConfirm;
  if (context == TouchContext::Music) {
    if (gesture == TouchGesture::SwipeLeft) return TouchRoute::NextTrack;
    if (gesture == TouchGesture::SwipeRight) return TouchRoute::PreviousTrack;
    return TouchRoute::None;
  }
  if (context == TouchContext::Launcher || context == TouchContext::Secondary) {
    if (gesture == TouchGesture::SwipeLeft) return TouchRoute::Next;
    if (gesture == TouchGesture::SwipeRight) return TouchRoute::Previous;
  } else if (context == TouchContext::Menu) {
    if (gesture == TouchGesture::SwipeUp) return TouchRoute::Next;
    if (gesture == TouchGesture::SwipeDown) return TouchRoute::Previous;
  }
  return TouchRoute::None;
}

} // namespace smart_speaker
