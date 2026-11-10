#pragma once
#include "app/execution/navigation_policy.h"
#include "lunaros_app_manager_application.h"
#include "input/navigation_repeat.h"

namespace lunaredu::lunaros::apps::manager {
struct NavigationInput {
  int step = 0, axis_x = 0, axis_y = 0;
  bool previous_page = false, next_page = false;
  bool discover = false, installed = false, confirm = false, back = false;
  uint32_t now_ms=0;
  int secondary_x=0,secondary_y=0;
  bool axes_fresh=true;
};
struct NavigationResult { bool changed = false, back_consumed = false; };
// Logical input only. Product maps its physical keys/axes; this App owns list,
// tab, detail, uninstall and save-confirmation navigation.
class Navigation {
 public:
  using ActionSink = void (*)(void *, uint32_t);
  void reset() { stick_.reset(); }
  NavigationResult update(View &, const StoreSnapshot &, NavigationInput,
                          void *, ActionSink);
 private:
  lunaredu::app::ApplicationNavigationRepeat stick_;
};
} // namespace lunaredu::lunaros::apps::manager
