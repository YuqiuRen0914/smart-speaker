#pragma once
#include "desktop/navigation/view_navigation.h"
#include "desktop/shell/shell.h"
#include "graphics/scene_runtime.h"
#include "desktop/ui/touch_router.h"
#include <cstring>
namespace lunaredu::desktop {
// OS owns return-location restoration and old-contact retirement. App hooks
// perform checked business exit; a rejected transition never commits a view.
inline void restore_shell_location(DesktopShell &shell,const ViewNavigationLocation &location) {
  const bool home_requested=shell.surface==DesktopSurface::Launcher;
  shell.surface=DesktopSurface::Launcher;shell.select(static_cast<size_t>(location.launcher_index));
  if(home_requested||!location.menu||!shell.open_menu(location.menu))return;
  const auto *menu=shell.active_menu;
  if(location.item)for(size_t i=0;i<menu->count;++i)if(!std::strcmp(menu->items[i].id,location.item)) {
    shell.row=static_cast<uint8_t>(i);shell.list_offset=i>=3?static_cast<uint8_t>(i-2):0;break;
  }
}
template<class Switch> bool commit_view_transition(app::ViewRoute next,app::ViewRoute &current,
    const char *foreground,Switch switch_foreground,lunaros::graphics::SceneRuntime &graphics,
    DesktopShell &shell,TouchInputRouter &touch,ViewNavigationLocation &location,bool touch_available,
    bool restore_previous_shell=true) {
  if(!switch_foreground(foreground))return false;
  graphics.invalidate();touch.cancel();shell.cancel_contact();shell.status_navigation.cancel();
  if(next!=kShellView)shell.launch=-1;
  else if(current!=kShellView&&restore_previous_shell)restore_shell_location(shell,location);
  current=next;
  const bool navigation=touch_available&&(next!=kShellView||shell.surface!=DesktopSurface::Launcher);
  shell.home_navigation.reset(navigation);shell.edge_navigation.reset(navigation);
  return true;
}
}
