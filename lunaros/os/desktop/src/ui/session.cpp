#include "desktop/navigation/runtime.h"
#include "desktop/ui/session.h"
namespace lunaredu::desktop {
  UiControlEvents UiSession::controls(UiControlSample input) {
    if(input.defer_desktop_buttons){
      const auto mask=uint16_t(input.confirm_mask|input.back_mask);
      const auto held=uint16_t(input.buttons&mask);
      UiControlEvents result{false,input.edge_back,static_cast<int16_t>(input.encoder_position-state_.last_encoder_position)};
      const auto emit=[&]{
        result.confirm=bool(state_.pending_desktop_button&input.confirm_mask);
        result.back|=bool(state_.pending_desktop_button&input.back_mask);
        state_.desktop_button_dispatched=true;state_.pending_desktop_button=0;
      };
      if(held==mask&&mask){
        const bool late=state_.pending_desktop_button&&uint32_t(input.now_ms-state_.desktop_button_started)>=80;
        if(late&&!state_.desktop_combo_latched&&!state_.desktop_button_dispatched)emit();
        result.combo=!late&&!state_.desktop_combo_latched&&!state_.desktop_button_dispatched;
        state_.desktop_combo_latched=true;state_.pending_desktop_button=0;
      }else if(!held){
        if(!state_.desktop_combo_latched&&!state_.desktop_button_dispatched)emit();
        state_.pending_desktop_button=0;state_.desktop_combo_latched=false;state_.desktop_button_dispatched=false;
      }else if(!state_.desktop_combo_latched&&!state_.desktop_button_dispatched){
        if(!state_.pending_desktop_button){state_.pending_desktop_button=held;state_.desktop_button_started=input.now_ms;}
        else if(state_.pending_desktop_button!=held)emit();
        if(state_.pending_desktop_button&&uint32_t(input.now_ms-state_.desktop_button_started)>=80)emit();
      }
      return result;
    }
    state_.pending_desktop_button=0;
    state_.desktop_button_dispatched=false;
    return {pressed(input.buttons, input.confirm_mask),
        pressed(input.buttons, input.back_mask) || input.edge_back,
        static_cast<int16_t>(input.encoder_position - state_.last_encoder_position)};
  }
  TouchEvent UiSession::pointer(const TouchInput &touch, int64_t now_us, bool blocked,
      bool reserved, bool physical_activity) {
    if (blocked || physical_activity) state_.touch_router.cancel();
    return blocked || reserved ? TouchEvent{} : state_.touch_router.update(touch, now_us);
  }
  int UiSession::navigation_delta(bool confirm, bool back, int touch, int encoder,
      bool axis_enabled, int horizontal, int vertical, uint32_t now_ms, unsigned rotation) {
    int result = confirm || back ? 0 : touch ? touch : encoder;
    if(!axis_enabled||confirm||back||result)state_.stick_navigation.reset();
    else {
      const auto axis=lunaros::input::rotate_navigation({horizontal,vertical},rotation);
      result=lunaros::input::navigation_step(state_.stick_navigation.update(axis.x,axis.y,now_ms));
    }
    return result;
  }
  void UiSession::observe(uint16_t buttons, int16_t encoder) {
    state_.last_buttons = buttons; state_.last_encoder_position = encoder;
  }
  void UiSession::released(bool released, bool driving_view) {
    if (released) state_.wait_for_button_release = false;
    if (!driving_view) state_.wait_for_button_release = true;
  }
  void UiSession::rearm(DesktopShell &shell, lunaros::graphics::SceneRuntime &graphics) {
    ++state_.input_epoch;
    state_.wait_for_button_release = true;
    state_.stick_navigation.reset();
    state_.stick_merger.reset();
    state_.pending_desktop_button=0;
    state_.touch_router.cancel(); shell.cancel_contact(); shell.status_navigation.cancel();
    graphics.invalidate(); state_.force_redraw = true;
  }
  void UiSession::suspend_navigation(DesktopShell &shell) {
    state_.stick_navigation.reset();
    state_.stick_merger.reset();
    shell.home_navigation.reset(); shell.edge_navigation.reset();
    state_.touch_router.cancel(); shell.cancel_contact(); state_.wait_for_button_release = true;
  }
  void UiSession::select_widgets(DesktopShell &shell, lunaros::graphics::SceneRuntime &graphics,
      uint64_t key, bool shell_view, bool widgets) {
    const auto before = graphics.page();
    const auto after = shell_view ? lunaros::graphics::select_desktop(graphics,key, shell) : graphics.select(key, widgets);
    if (before.generation != after.generation) state_.force_redraw = true;
  }
  bool UiSession::pressed(uint16_t buttons, uint16_t mask) const {
    return (buttons & mask) != 0 && (state_.last_buttons & mask) == 0;
  }
}

namespace lunaredu::desktop {
NavigationResult update_navigation(DesktopShell &shell, const NavigationInput &input,
                                         NavigationLifecycle &lifecycle) {
  const auto now = input.now_ms;
  const bool animations = shell.preferences.animations;
  const bool home_animations = animations && input.home_card_animation;
  auto &home = shell.home_navigation;
  if (home.enabled != input.home_enabled && !home.exiting()) home.reset(input.home_enabled);
  if (input.blocked || (home.contact && input.physical_activity)) home.cancel(now, home_animations);
  if (input.blocked) home.reset();
  const float previous_progress = home.progress, previous_opacity = home.opacity;
  const bool previous_contact = home.contact, previous_visible = home.visible, previous_moving = home.moving;
  bool home_changed = home.tick(now, home_animations);
  const bool fresh = fresh_single_touch(input.touch, now);
  const auto point = input.rotate_touch ? shell.logical(input.touch.x, input.touch.y) :
      DesktopPoint{static_cast<float>(input.touch.x), static_cast<float>(input.touch.y)};
  auto &top = shell.status_navigation;
  if (input.physical_activity) { top.activity(now); if (top.contact) top.cancel(); }
  const bool top_before = top.showing(shell.preferences.auto_hide_status);
  bool top_changed = top.update(now, shell.preferences.auto_hide_status, input.attention, input.low_battery);
  const bool top_owned = top.pointer(input.touch.pressed, input.touch.available && fresh && !input.blocked,
      point.x, point.y, now, shell.preferences.auto_hide_status);
  top_changed |= top_before != top.showing(shell.preferences.auto_hide_status);
  const bool top_system = top.take_system();
  if (top_owned) lifecycle.cancel_input(NavigationInputOwner::Status);
  if (top_changed) lifecycle.render(NavigationPaint::Full);
  // The valid release must also clear other gesture guards.
  const bool release = !input.touch.pressed && fresh;
  const bool home_owned = home.pointer(input.touch.pressed,
      fresh && !input.blocked && (!top_owned || release), point.x, point.y, now, home_animations);
  home_changed |= previous_progress != home.progress || previous_opacity != home.opacity ||
      previous_contact != home.contact || previous_visible != home.visible || previous_moving != home.moving;
  if (home.take_request()) {
    std::string error;
    if (!lifecycle.leave_for_home(error)) {
      home.reset(input.home_enabled); lifecycle.exit_rejected(error);
      lifecycle.render(NavigationPaint::Full); return {true};
    }
  }
  if (home.take_finished()) { lifecycle.home_finished(); return {true}; }
  if (home_owned || home.progress > 0 || home.moving) {
    lifecycle.cancel_input(NavigationInputOwner::Home);
    if (home_changed) lifecycle.render(NavigationPaint::Animation);
    return {true};
  }
  if (home_changed) lifecycle.render(NavigationPaint::Animation);
  auto &edge = shell.edge_navigation;
  if (edge.enabled != input.edge_enabled) edge.reset(input.edge_enabled);
  edge.top_inset=top.showing(shell.preferences.auto_hide_status)?status_layout::height:8;
  const float previous_edge = edge.progress, previous_y = edge.y;
  const bool previous_ready = edge.ready, previous_edge_moving = edge.moving;
  // Validate before tick: stale contacts cannot finish a delayed Back.
  const bool edge_owned = edge.pointer(input.touch.pressed,
      input.touch.available && fresh && !input.blocked && !input.edge_interrupted && (!top_owned || release),
      point.x, point.y, now, animations);
  bool edge_changed = edge.tick(now, animations);
  edge_changed |= previous_edge != edge.progress || previous_y != edge.y ||
      previous_ready != edge.ready || previous_edge_moving != edge.moving;
  const bool back = edge.take_finished();
  if (back) edge.reset(input.edge_enabled);
  if (back && lifecycle.back_finished()) return {true};
  if (!back && (edge_owned || edge.progress > 0 || edge.moving)) {
    lifecycle.cancel_input(NavigationInputOwner::Edge);
    if (edge_changed) lifecycle.render(NavigationPaint::Animation);
    return {true};
  }
  if (edge_changed) lifecycle.render(NavigationPaint::Animation);
  return {false, top_owned, back, top_system, now};
}
}
