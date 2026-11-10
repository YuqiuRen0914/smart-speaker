#pragma once
#include "desktop/ui/system_views.h"
#include "desktop/shell/shell.h"
namespace lunaredu::desktop {
enum class UiEntryKind { View, Home, Menu, Surface, Tile };
struct UiEntry {
  app::ViewRoute route=kShellView;
  UiEntryKind kind=UiEntryKind::View;
  const char *id=nullptr;
  DesktopSurface surface=DesktopSurface::Launcher;
};
// Apps with a desktop-owned surface only declare their entry. Checked route
// commitment and foreground/resource admission stay with the consuming host.
template<class Bindings, UiEntry (*Entry)()>
typename Bindings::Definition entry_ui_binding(const char *id) {
  return {id, nullptr, nullptr, [](typename Bindings::EntryContext &c) { c.enter_ui(Entry()); }};
}

}
