#include "smart_speaker_touch_routing.h"
#include <cassert>
#include <initializer_list>
using namespace smart_speaker;
int main() {
  assert(route_touch_gesture(TouchGesture::SwipeLeft, TouchContext::Music) == TouchRoute::NextTrack);
  assert(route_touch_gesture(TouchGesture::SwipeRight, TouchContext::Music) == TouchRoute::PreviousTrack);
  assert(route_touch_gesture(TouchGesture::Tap, TouchContext::Music) == TouchRoute::MenuConfirm);
  for (auto gesture : {TouchGesture::SwipeUp, TouchGesture::SwipeDown,
       TouchGesture::LongPress, TouchGesture::LongPressRelease})
    assert(route_touch_gesture(gesture, TouchContext::Music) == TouchRoute::None);
  assert(route_touch_gesture(TouchGesture::SwipeLeft, TouchContext::Home) == TouchRoute::OpenProductShell);
  for (auto gesture : {TouchGesture::Tap, TouchGesture::SwipeUp, TouchGesture::SwipeDown, TouchGesture::SwipeRight})
    assert(route_touch_gesture(gesture, TouchContext::Home) == TouchRoute::None);
  assert(route_touch_gesture(TouchGesture::SwipeLeft, TouchContext::Launcher) == TouchRoute::Next);
  assert(route_touch_gesture(TouchGesture::SwipeRight, TouchContext::Launcher) == TouchRoute::Previous);
  assert(route_touch_gesture(TouchGesture::SwipeUp, TouchContext::Launcher) == TouchRoute::None);
  assert(route_touch_gesture(TouchGesture::SwipeDown, TouchContext::Launcher) == TouchRoute::None);
  assert(route_touch_gesture(TouchGesture::SwipeLeft, TouchContext::Secondary) == TouchRoute::Next);
  assert(route_touch_gesture(TouchGesture::SwipeRight, TouchContext::Secondary) == TouchRoute::Previous);
  assert(route_touch_gesture(TouchGesture::SwipeUp, TouchContext::Secondary) == TouchRoute::None);
  assert(route_touch_gesture(TouchGesture::SwipeDown, TouchContext::Secondary) == TouchRoute::None);
  assert(route_touch_gesture(TouchGesture::Tap, TouchContext::Secondary) == TouchRoute::MenuConfirm);
  assert(route_touch_gesture(TouchGesture::SwipeUp, TouchContext::Menu) == TouchRoute::Next);
  assert(route_touch_gesture(TouchGesture::SwipeDown, TouchContext::Menu) == TouchRoute::Previous);
  for (auto context : {TouchContext::Menu, TouchContext::Feature}) {
    assert(route_touch_gesture(TouchGesture::SwipeLeft, context) == TouchRoute::None);
    assert(route_touch_gesture(TouchGesture::SwipeRight, context) == TouchRoute::None);
  }
  for (auto context : {TouchContext::Launcher, TouchContext::Menu, TouchContext::Feature})
    assert(route_touch_gesture(TouchGesture::Tap, context) == TouchRoute::MenuConfirm);
  for (auto context : {TouchContext::Home, TouchContext::Launcher, TouchContext::Secondary, TouchContext::Menu, TouchContext::Feature, TouchContext::Blocked}) {
    assert(route_touch_gesture(TouchGesture::LongPress, context) == TouchRoute::None);
    assert(route_touch_gesture(TouchGesture::LongPressRelease, context) == TouchRoute::None);
  }
  for (auto gesture : {TouchGesture::Tap, TouchGesture::SwipeLeft, TouchGesture::SwipeRight, TouchGesture::SwipeUp, TouchGesture::SwipeDown})
    assert(route_touch_gesture(gesture, TouchContext::Blocked) == TouchRoute::None);
}
