#pragma once
#include "lunaros_app_manager_navigation.h"
#include "desktop/ui/touch_router.h"
#include "desktop/shell/shell.h"
#include "graphics/scene_runtime.h"

namespace lunaredu::lunaros::apps::manager {
// Coordinates and commands are logical. The product retains physical mapping
// and grants permission before invoking this on the existing input executor.
struct DesktopInteractionInput {
  desktop::TouchInput touch;
  int64_t now_us = 0;
  int encoder = 0, axis_x = 0, axis_y = 0, command = -1;
  bool reserved_contact = false, confirm = false, back = false;
  int secondary_x=0,secondary_y=0;
  bool axes_fresh=true;
};
struct DesktopInteractionResult {
  bool changed = false, back_consumed = false, request_home = false, wake_graphics = false;
};
class DesktopInteraction {
 public:
  void reset() { navigation_.reset(); }
  template<class InputPort> DesktopInteractionResult update(View &view, const StoreSnapshot &snapshot,
      InputPort &port,
      const DesktopInteractionInput &input, void *context, Navigation::ActionSink action,
      bool (*visible)(void *)) {
    DesktopInteractionResult result;
    if (!action || !visible) return result;
    const bool pull_enabled = view.detail_id.empty() && !busy(snapshot) && snapshot.phase != StorePhase::Fetching;
    auto touch_input = input.touch;
    if (input.reserved_contact) touch_input.valid = false;
    const auto touch = port.update_touch(touch_input, input.now_us, pull_enabled);
    const int pull = pull_enabled ? port.pull_refresh_distance(input.now_us) : 0;
    if (pull != view.pull_distance) { view.pull_distance = pull; result.changed = true; }
    if (touch.gesture == desktop::TouchGesture::Tap)
      result.wake_graphics = port.tap(int(touch.x), int(touch.y));
    if (touch.gesture == desktop::TouchGesture::PullDown) { action(context, Refresh); result.changed = true; }
    graphics::Action widget_action;
    while (port.take_action(widget_action)) {
      if (!visible(context)) continue;
      action(context, widget_action.id); result.changed = true;
    }
    const auto command = input.command;
    const auto is = [&](desktop::DesktopCommand value) { return command == int(value); };
    NavigationInput navigation;
    navigation.step = input.encoder;
    if (is(desktop::DesktopCommand::Next) || is(desktop::DesktopCommand::Down)) navigation.step = 1;
    if (is(desktop::DesktopCommand::Previous) || is(desktop::DesktopCommand::Up)) navigation.step = -1;
    navigation.axis_x = input.axis_x; navigation.axis_y = input.axis_y;
    navigation.now_ms=uint32_t(input.now_us/1000);
    navigation.secondary_x=input.secondary_x;navigation.secondary_y=input.secondary_y;navigation.axes_fresh=input.axes_fresh;
    navigation.next_page = touch.gesture == desktop::TouchGesture::Next;
    navigation.previous_page = touch.gesture == desktop::TouchGesture::Previous;
    navigation.discover = is(desktop::DesktopCommand::Left);
    navigation.installed = is(desktop::DesktopCommand::Right);
    navigation.confirm = input.confirm || is(desktop::DesktopCommand::Confirm);
    navigation.back = input.back || is(desktop::DesktopCommand::Back);
    const auto navigated = navigation_.update(view, snapshot, navigation, context, action);
    result.changed |= navigated.changed; result.back_consumed = navigated.back_consumed;
    result.request_home = !navigated.back_consumed && navigation.back;
    return result;
  }
 private:
  Navigation navigation_;
};
} // namespace lunaredu::lunaros::apps::manager
