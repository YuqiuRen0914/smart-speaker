#pragma once
#include "desktop/shell/shell.h"
#include <cstring>

namespace lunaredu::desktop {
struct DesktopMenuOwner {
  const char *menu_prefix;
  const char *foreground_id;
};
// Surface extensions publish their own identity; product menu families are
// declarative. Focusing a launcher tile never changes the foreground owner.
inline const char *desktop_foreground(const DesktopShell &shell, const char *desktop_id,
    const char *settings_id, const DesktopMenuOwner *menus = nullptr, size_t count = 0) {
  const auto &extension = desktop_surface_extension(shell,shell.surface);
  if (extension.foreground_id && *extension.foreground_id) return extension.foreground_id;
  if (extension.application && extension.application->id) return extension.application->id;
  if (shell.surface == DesktopSurface::Actions) {
    const char *id = shell.active_menu ? shell.active_menu->id : nullptr;
    if (id) for (size_t i = 0; menus && i < count; ++i) {
      if (menus[i].menu_prefix && menus[i].foreground_id &&
          !std::strncmp(id, menus[i].menu_prefix, std::strlen(menus[i].menu_prefix)))
        return menus[i].foreground_id;
    }
    return settings_id && *settings_id ? settings_id : desktop_id;
  }
  // Wifi uses the core Shell implementation; optional surfaces declare their
  // owner through the extension above, including ApplicationData and Status.
  if (shell.surface == DesktopSurface::Wifi || shell.surface == DesktopSurface::Notifications)
    return settings_id && *settings_id ? settings_id : desktop_id;
  return desktop_id;
}
} // namespace lunaredu::desktop
