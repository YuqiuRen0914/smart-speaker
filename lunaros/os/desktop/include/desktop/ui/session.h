#pragma once
#include "app/execution/navigation_policy.h"
#include "desktop/ui/touch_router.h"
#include "desktop/navigation/view_navigation.h"
#include "desktop/graphics/desktop.h"
#include "input/navigation_repeat.h"
#include <string>

namespace lunaredu::desktop {
// Logical input and presentation coordination; no concrete App, driver or
// physical button choice. The composition retains storage on its input owner.
struct UiSessionState {
  TouchInputRouter touch_router;
  ViewNavigationLocation navigation;
  bool touch_available = false, ready = false, have_model = false;
  bool force_redraw = true, wait_for_button_release = false;
  lunaredu::app::ApplicationNavigationRepeat stick_navigation;
  lunaredu::app::ApplicationNavigationMerger stick_merger;
  int64_t last_dynamic_refresh_us = 0;
  uint32_t input_epoch=0;
  uint16_t last_buttons = 0;
  uint16_t pending_desktop_button=0;
  bool desktop_combo_latched=false;
  bool desktop_button_dispatched=false;
  uint32_t desktop_button_started=0;
  int16_t last_encoder_position = 0;
};
struct UiControlSample {
  uint16_t buttons = 0, confirm_mask = 0, back_mask = 0;
  int16_t encoder_position = 0;
  bool edge_back = false;
  bool defer_desktop_buttons=false;
  uint32_t now_ms=0;
};
struct UiControlEvents { bool confirm = false, back = false; int encoder = 0; bool combo=false; };
class UiSession {
 public:
  explicit UiSession(UiSessionState &state) : state_(state) {}
  UiSession(const UiSession &) = delete;
  UiSession &operator=(const UiSession &) = delete;
  template<class Prepare> bool prepare(Prepare action, std::string &error) {
    error.clear();
    if (prepare_attempted_) { error = "ui_preparation_already_attempted"; return false; }
    prepare_attempted_ = true; prepared_ = action(error); return prepared_;
  }
  template<class Declare> bool declare(Declare action, std::string &error) {
    error.clear();
    if (!prepared_) { error = "ui_not_prepared"; return false; }
    if (declare_attempted_) { error = "ui_registration_already_attempted"; return false; }
    declare_attempted_ = true; declared_ = action(error); return declared_;
  }
  template<class Start> bool start(Start action, std::string &error) {
    error.clear();
    if (!declared_) { error = "ui_applications_not_declared"; return false; }
    if (start_attempted_) { error = "ui_foreground_already_attempted"; return false; }
    start_attempted_ = true;
    if (!action(error)) return false;
    state_.ready = true; return true;
  }
  UiControlEvents controls(UiControlSample input);
  TouchEvent pointer(const TouchInput &touch, int64_t now_us, bool blocked,
      bool reserved, bool physical_activity);
  int navigation_delta(bool confirm, bool back, int touch, int encoder,
      bool axis_enabled, int horizontal, int vertical, uint32_t now_ms, unsigned rotation);
  void observe(uint16_t buttons, int16_t encoder);
  void released(bool released, bool driving_view);
  void rearm(DesktopShell &shell, lunaros::graphics::SceneRuntime &graphics);
  void suspend_navigation(DesktopShell &shell);
  void select_widgets(DesktopShell &shell, lunaros::graphics::SceneRuntime &graphics,
      uint64_t key, bool shell_view, bool widgets);
 private:
  bool pressed(uint16_t buttons, uint16_t mask) const;
  UiSessionState &state_;
  bool prepare_attempted_ = false, prepared_ = false;
  bool declare_attempted_ = false, declared_ = false, start_attempted_ = false;
};
}
