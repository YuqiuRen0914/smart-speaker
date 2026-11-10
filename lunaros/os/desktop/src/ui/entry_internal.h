#pragma once
#include "desktop/ui/entry.h"
namespace lunaredu::desktop {
inline app::ViewRoute apply_ui_entry(DesktopShell &shell,UiEntry entry) {
  switch(entry.kind) {
    case UiEntryKind::Home:shell.command(DesktopCommand::Home,true);break;
    case UiEntryKind::Menu:shell.open_menu(entry.id);break;
    case UiEntryKind::Tile:shell.activate(entry.id);break;
    case UiEntryKind::Surface:shell.surface=entry.surface;if(const auto open=desktop_surface_extension(shell,entry.surface).open)open(shell);break;
    case UiEntryKind::View:break;
  }
  return entry.route;
}
}
