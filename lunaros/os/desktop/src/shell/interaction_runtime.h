#pragma once
#include "app/execution/navigation_policy.h"
#include "desktop/navigation/runtime.h"
#include "desktop/graphics/desktop.h"
#include "input/navigation_repeat.h"
#include <algorithm>
#include <cmath>

namespace lunaredu::desktop {
struct DesktopContactResult { bool changed = false, generation_changed = false; };
// Coordinate conversion, reserved contacts and graphics page qualification are
// shared. The product supplies only physical activity and system ownership.
inline DesktopContactResult update_desktop_contact(DesktopShell &shell, lunaros::graphics::SceneRuntime &graphics,
    uint64_t page_key, const TouchInput &touch, uint32_t now_ms, bool top_owned, bool physical_activity,
    bool blocked = false) {
  const bool fresh = touch.available && fresh_single_touch(touch, now_ms);
  if (physical_activity) shell.cancel_contact();
  bool changed = false;
  if (!top_owned || !touch.pressed)
    changed = shell.pointer(touch.pressed, fresh, touch.x, touch.y, false);
  const auto before = graphics.page();
  const auto after = lunaros::graphics::select_desktop(graphics,page_key, shell);
  const auto point = shell.logical(touch.x, touch.y);
  const bool reserved = shell.home_navigation.contact || shell.edge_navigation.owned ||
      shell.home_navigation.moving || shell.edge_navigation.moving || shell.moving || shell.page_drag ||
      shell.surface == DesktopSurface::Launcher || blocked || physical_activity || top_owned;
  (void)graphics.pointer(touch.pressed, int(point.x), int(point.y), fresh && !top_owned, reserved);
  return {changed, before.generation != after.generation};
}
struct DesktopNavigationInput {
  bool system = false, back = false, confirm = false;
  int encoder = 0, horizontal = 0, vertical = 0;
  uint32_t now_ms=0;
  bool axes_enabled=true;
};
// Values already have the product's axis/button mapping. Neutral rearming and
// command priority are shared, with no knowledge of radio protocol bits.
inline bool update_desktop_commands(DesktopShell &shell, lunaredu::app::ApplicationNavigationRepeat &stick, const DesktopNavigationInput &input) {
  bool changed = false;
  if (input.system) changed = shell.command(DesktopCommand::System);
  else if (input.back) changed = shell.command(DesktopCommand::Back);
  else if (input.confirm) changed = shell.command(DesktopCommand::Confirm);
  else if (input.encoder) {
    const int steps = std::min(std::abs(input.encoder), 32);
    for (int i = 0; i < steps; ++i)
      changed |= shell.command(input.encoder > 0 ? DesktopCommand::Next : DesktopCommand::Previous);
  }
  if(input.system||input.confirm||input.back||input.encoder){stick.reset();shell.reset_joystick();}
  else if(!input.axes_enabled){stick.reset();shell.reset_joystick();}
  else if(shell.surface==DesktopSurface::Launcher&&shell.preferences.layout==DesktopLayout::Honeycomb&&
      (!shell.compact.enabled||shell.compact.mode==CompactMode::Desktop||shell.compact.mode==CompactMode::Edit)){
    stick.reset();changed|=shell.joystick(input.horizontal,input.vertical,input.now_ms);
  }
  else {
    using D=lunaros::input::NavigationDirection;
    const auto direction=stick.update(input.horizontal,input.vertical,input.now_ms);
    if(direction!=D::None)changed|=shell.command(direction==D::Right?DesktopCommand::Right:
        direction==D::Left?DesktopCommand::Left:direction==D::Down?DesktopCommand::Down:DesktopCommand::Up);
  }
  return changed;
}
inline bool persist_desktop_preferences(DesktopShell &shell, bool (*save)(DesktopPreferences)) {
  if (!shell.preferences_changed) return true;
  shell.preferences_save_failed = !save || !save(shell.preferences);
  shell.preferences_changed = false;
  return !shell.preferences_save_failed;
}
} // namespace lunaredu::desktop
