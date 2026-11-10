#pragma once
#include "desktop/graphics/canvas_spec.h"
#include "desktop/shell/shell.h"
#include "desktop/ui/touch_input.h"
#include <string>

namespace lunaredu::desktop {
inline bool fresh_single_touch(const TouchInput &touch, uint32_t now_ms,
                               unsigned width = canvas_layout::side, unsigned height = canvas_layout::side) {
  return touch.valid && now_ms - touch.sampled_at_ms <= 120 && touch.contact_count <= 1 &&
      touch.contact_count == (touch.pressed ? 1 : 0) &&
      (!touch.pressed || (touch.x < width && touch.y < height));
}
enum class NavigationPaint { Full, Animation, Menu };
enum class NavigationInputOwner { Status, Home, Edge };
struct NavigationInput {
  TouchInput touch;
  uint32_t now_ms = 0;
  bool blocked = false, home_enabled = false, edge_enabled = false;
  bool rotate_touch = false, physical_activity = false, edge_interrupted = false;
  bool attention = false, low_battery = false;
  bool home_card_animation = true;
};
struct NavigationResult {
  bool consumed = false, top_owned = false, edge_back_finished = false, top_system = false;
  uint32_t now_ms = 0;
};
class NavigationLifecycle {
 public:
  virtual ~NavigationLifecycle() = default;
  virtual void cancel_input(NavigationInputOwner) = 0;
  // Checked exit and product cleanup complete before the Home animation.
  virtual bool leave_for_home(std::string &) = 0;
  virtual void exit_rejected(const std::string &) = 0;
  virtual void home_finished() = 0;
  virtual bool back_finished() = 0;
  virtual void render(NavigationPaint) = 0;
};
// Input-owner coordinator. It knows no product page, radio policy, App type or
// physical key mapping. Gesture state stays in the Shell and render snapshots.
NavigationResult update_navigation(DesktopShell &shell, const NavigationInput &input,
                                         NavigationLifecycle &lifecycle);
} // namespace lunaredu::desktop
