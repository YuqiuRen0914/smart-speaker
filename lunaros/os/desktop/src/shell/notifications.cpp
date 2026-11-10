#include "desktop/shell/notifications.h"
#include <algorithm>
#include "desktop/ui/content_layout.h"
#include <new>
#include <chrono>
#include <cstdio>
#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#include <esp_timer.h>
#endif

namespace lunaredu::desktop::notifications {
namespace {
const char state_kind = 0;
struct Clock { uint64_t (*read)(); };
uint64_t notice_time(const DesktopShell &shell) {
  const auto context=desktop_surface_context(shell,DesktopSurface::Notifications,extension());
  if(context)return std::static_pointer_cast<const Clock>(context)->read();
#ifdef ESP_PLATFORM
  // Notice expiry uses the full monotonic clock, beyond the desktop's 32-bit tick.
  return uint64_t(esp_timer_get_time() / 1000);
#else
  return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count());
#endif
}
} // namespace
State::State() : DesktopSurfaceState(&state_kind) {}
const State *state(const desktop::DesktopShell &shell) {
  return shell.surface_state && shell.surface_state->kind == &state_kind
      ? static_cast<const State *>(shell.surface_state.get()) : nullptr;
}
bool refresh(desktop::DesktopShell &shell) {
  const auto *previous = state(shell);
  if (previous && previous->notices.revision ==
      lunaros::notifications::revision(notice_time(shell))) return false;
  auto next = std::shared_ptr<State>(new(std::nothrow) State);
  if (!next) return false;
  lunaros::notifications::snapshot(next->notices, notice_time(shell));
  shell.row = uint8_t(next->notices.count
      ? std::min<size_t>(shell.row, next->notices.count - 1) : 0);
  shell.surface_state = std::move(next);
  ++shell.surface_generation;
  return true;
}
void build(lunaros::graphics::Scene &, const desktop::DesktopShell &,
           const desktop::DesktopStatus &);
bool action(desktop::DesktopShell &, lunaros::graphics::Action);
const desktop::DesktopSurfaceExtension &extension() {
  static const desktop::DesktopSurfaceExtension value = [] {
    desktop::DesktopSurfaceExtension e;
    e.open = [](desktop::DesktopShell &shell) {
      shell.surface = desktop::DesktopSurface::Notifications;
      shell.row = 0;
      shell.surface_state.reset();
      refresh(shell);
    };
    e.close = [](desktop::DesktopShell &shell) { shell.surface_state.reset(); };
    e.poll = [](desktop::DesktopShell &shell, uint32_t now) {
      shell.now_ms = now;
      return refresh(shell);
    };
    e.command = [](desktop::DesktopShell &shell, desktop::DesktopCommand command) {
      using desktop::DesktopCommand;
      if (command == DesktopCommand::Back) {
        const auto *parent=shell.leaf_menu; const auto row=shell.leaf_row;
        if (!parent || !shell.open_menu(parent->id)) {
          if (shell.settings_id && shell.open_menu(shell.settings_id)) return true;
          shell.surface = DesktopSurface::Launcher;
          shell.surface_state.reset(); return true;
        }
        shell.row=row; const auto rows=desktop::ContentLayout(shell.preferences.auto_hide_status).rows();
        shell.list_offset=row>=rows?row-rows+1:0; return true;
      }
      // Confirm the displayed snapshot first. Refreshing before acknowledgement
      // could silently target a replacement notice at the same list position.
      if (command != DesktopCommand::Confirm) refresh(shell);
      const auto *view = state(shell);
      if (!view || !view->notices.count) return true;
      if (command == DesktopCommand::Confirm) {
        if (shell.row < view->notices.count) {
          const auto &entry = view->notices.entries[shell.row];
          lunaros::notifications::acknowledge(entry.app_id.data(), entry.id.data(),
              view->notices.revision, notice_time(shell));
        }
        refresh(shell);
        return true;
      }
      const bool previous = command == DesktopCommand::Previous ||
          command == DesktopCommand::Up || command == DesktopCommand::Left;
      shell.row = uint8_t((shell.row + view->notices.count +
          (previous ? -1 : 1)) % view->notices.count);
      return true;
    };
    e.revision = [](const desktop::DesktopShell &shell) {
      return uint64_t(shell.surface_generation) * 37 + shell.row + 1;
    };
    e.title = [](const desktop::DesktopShell &) { return "通知与提醒"; };
    e.badge = [](const desktop::DesktopShell &shell) {
      const auto summary = lunaros::notifications::summary(nullptr, notice_time(shell));
      return summary.unread || summary.pending;
    };
    e.build = build;
    e.action = action;
    return e;
  }();
  return value;
}
bool install_surface(DesktopShell &shell,uint64_t (*clock_ms)()) {
  const auto context=desktop_surface_context(shell,DesktopSurface::Notifications,extension());
  if(&desktop_surface_extension(shell,DesktopSurface::Notifications)==&extension()) {
    if(!clock_ms || (context&&std::static_pointer_cast<const Clock>(context)->read==clock_ms))return true;
    if(!remove_desktop_surface(shell,DesktopSurface::Notifications,extension(),context))return false;
  }
  return install_desktop_surface(shell,DesktopSurface::Notifications,extension(),
      clock_ms?std::make_shared<const Clock>(Clock{clock_ms}):nullptr);
}
bool remove_surface(DesktopShell &shell) {
  return remove_desktop_surface(shell,DesktopSurface::Notifications,extension(),
      desktop_surface_context(shell,DesktopSurface::Notifications,extension()));
}
} // namespace lunaredu::desktop::notifications
