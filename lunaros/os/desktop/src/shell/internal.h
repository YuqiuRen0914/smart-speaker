#pragma once
#include "desktop/shell/shell.h"
#include <array>
#include "navigation/honeycomb_layout.h"
#include "desktop/navigation/home_gesture.h"
#include "lunaros_ui_design.generated.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <cstdlib>


namespace lunaredu::desktop::shell_detail {
inline float distance(DesktopPoint a, DesktopPoint b) { return std::hypot(a.x-b.x,a.y-b.y); }
inline bool valid_name(const char *name) {
  if (!name || !*name) return false;
  size_t chars=0;
  for (const auto *p=reinterpret_cast<const unsigned char *>(name); *p;) {
    unsigned bytes=*p<0x80?1:(*p>=0xc2&&*p<=0xdf?2:(*p>=0xe0&&*p<=0xef?3:(*p>=0xf0&&*p<=0xf4?4:0)));
    if (!bytes || ++chars>4) return false;
    for (unsigned n=1;n<bytes;++n) if (!p[n] || (p[n]&0xc0)!=0x80) return false;
    if ((bytes==3 && ((*p==0xe0 && p[1]<0xa0)||(*p==0xed && p[1]>=0xa0))) ||
        (bytes==4 && ((*p==0xf0 && p[1]<0x90)||(*p==0xf4 && p[1]>=0x90)))) return false;
    p+=bytes;
  }
  return true;
}
inline const DesktopSurfaceExtension *application_surface(const DesktopShell &shell,const char *id, DesktopSurface *surface=nullptr) {
  for(unsigned i=0;i<unsigned(DesktopSurface::Count);++i) {
    const auto slot=static_cast<DesktopSurface>(i);const auto &extension=desktop_surface_extension(shell,slot);
    if(extension.open && extension.application && !std::strcmp(extension.application->id,id)) {
      if (surface) *surface = slot;
      return &extension;
    }
  }
  return nullptr;
}
inline void enter(DesktopShell &shell, size_t index) {
  if(index>=shell.count)return;
  DesktopSurface slot=DesktopSurface::Launcher;
  const auto *extension=shell.apps[index].kind==DesktopAppKind::Surface?application_surface(shell,shell.apps[index].id,&slot):nullptr;
  if(shell.apps[index].kind==DesktopAppKind::Surface&&!extension)return;
  if(const auto close=desktop_surface_extension(shell,shell.surface).close)close(shell);
  shell.select(index);shell.leaf_menu=nullptr;shell.row=0;shell.list_offset=0;
  if(extension){shell.surface=slot;extension->open(shell);}else shell.launch=static_cast<int>(index);
  shell.cancel_contact();
}
inline void choose_row(DesktopShell &s) {
  if(s.surface==DesktopSurface::Actions&&s.active_menu&&s.row<s.active_menu->count){
    const auto &item=s.active_menu->items[s.row];
    if(item.submenu)s.open_menu(item.id);
    else {
      for(unsigned i=0;i<static_cast<unsigned>(DesktopSurface::Count);++i)
        if(const auto action=desktop_surface_extension(s,static_cast<DesktopSurface>(i)).menu_action;action&&action(s,item))return;
      s.action=item.id;s.cancel_contact();
    }
  } else if (s.surface==DesktopSurface::System) {
    DesktopSurface destinations[static_cast<size_t>(DesktopSurface::Count)]{};const auto count=system_surfaces(s,destinations,static_cast<size_t>(DesktopSurface::Count));if(s.row>=count)return;
    s.leaf_menu=nullptr;
    s.settle_motion(); s.surface=destinations[s.row]; s.row=0; s.list_offset=0; s.cancel_contact();
    if(const auto open=desktop_surface_extension(s,s.surface).open)open(s);
  } else if (const auto choose=desktop_surface_extension(s,s.surface).choose) {
    choose(s);
  }
}
}
