#include "lunaros_settings_menu.h"
#include "desktop/shell/notifications.h"
#include "desktop/ui/content_layout.h"
#include "lunaros_settings_wifi.h"
#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif
namespace lunaredu::settings {void build_wifi_surface(lunaros::graphics::Scene &,const desktop::DesktopShell &,const desktop::DesktopStatus &);bool apply_wifi_action(desktop::DesktopShell &,lunaros::graphics::Action);}
#include "lunaros_settings_application_data.h"
#include <cstring>
#include "lunaros_settings_renderer.h"

namespace lunaredu::settings {
namespace {
using namespace desktop;
bool command(DesktopShell &s, DesktopCommand command) {
  if (command != DesktopCommand::Back) return false;
  if (s.surface == DesktopSurface::Startup && s.leaf_menu) {
    const auto *parent=s.leaf_menu;const auto previous=s.leaf_row;
    if (!s.open_menu(parent->id)) return false;
    s.row=previous;const int visible=ContentLayout(s.preferences.auto_hide_status).rows();s.list_offset=s.row>=visible?s.row-visible+1:0;return true;
  }
  return s.surface==DesktopSurface::Settings && s.menu_count && s.open_menu("os.settings");
}
bool menu_action(DesktopShell &s, const DesktopMenuItem &item) {
  if (!std::strcmp(item.id,"os.notice_center")) {
    s.leaf_menu=s.active_menu;s.leaf_row=s.row;s.cancel_contact();
    desktop::notifications::extension().open(s);return true;
  }
  if (!std::strcmp(item.id,"os.wifi")) {s.leaf_menu=s.active_menu;s.leaf_row=s.row;open_desktop_wifi(s);return true;}
  if (!std::strcmp(item.id,"os.applications")) return open_application_data(s);
  if (!std::strcmp(item.id,"os.desktop")) {
    s.surface=DesktopSurface::Settings;s.row=s.list_offset=0;s.cancel_contact();return true;
  }
  if (item.preference==DesktopPreferenceAction::Startup) { open_startup(s);return true; }
  if (item.preference!=DesktopPreferenceAction::None) { apply_preference(s,item.preference);return true; }
  return false;
}
DesktopSurfaceExtension make_extension(bool startup) {
  DesktopSurfaceExtension extension;
  extension.foreground_id="os.settings";
  extension.open=[](DesktopShell &s) { if(s.surface==DesktopSurface::Startup)open_startup(s);else open(s); };
  extension.command=command;
  extension.build=[](lunaros::graphics::Scene &scene,const DesktopShell &s,const DesktopStatus &) { build_preferences_surface(scene,s); };
  extension.rows=[](const DesktopShell &s) -> size_t { return s.surface==DesktopSurface::Startup?s.count+1:lunaros::graphics::metrics::native?8:5; };
  extension.choose=[](DesktopShell &s) {
    if(s.surface==DesktopSurface::Startup)choose_startup(s);else choose_basic_preference(s);
  };
  extension.title=[](const DesktopShell &s) { return s.surface==DesktopSurface::Startup?"开机应用":"桌面设置"; };
  if (!startup) {
    extension.menu_action=menu_action;
    extension.menu_value=preference_value;
    extension.menu_selected=preference_selected;
    extension.application=&desktop_application();
    extension.system_menu_order=2;
  }
  return extension;
}
}
const desktop::DesktopSurfaceExtension &desktop_extension(desktop::DesktopSurface surface) {
  static const auto settings=make_extension(false),startup=make_extension(true);
  static const auto wifi=[] {
    DesktopSurfaceExtension value;value.foreground_id="os.settings";value.open=open_desktop_wifi;value.close=close_desktop_wifi;value.command=wifi_command;value.poll=wifi_tick;
    value.title=[](const DesktopShell &){return "网络连接";};
    value.revision=[](const DesktopShell &s)->uint64_t {const auto w=wifi_browser(s);if(!w)return 1;uint64_t n=uint64_t(w->generation)*131+unsigned(w->view)+reinterpret_cast<uintptr_t>(w->content.get());if(w->view==WifiView::Password)n=n*131+unsigned(w->input.mode)+(w->input.group+1)*17+w->input.symbol_page*31+w->input.upper;return n?n:1;};
    value.build=build_wifi_surface;value.action=apply_wifi_action;
    return value;
  }();
  if(surface==desktop::DesktopSurface::Wifi)return wifi;
  return surface==desktop::DesktopSurface::Startup?startup:settings;
}
bool install_desktop_surfaces(desktop::DesktopShell &shell) {
  using namespace desktop;
  const bool present=desktop_surface_extension(shell,DesktopSurface::Settings).open;
  const auto &settings=desktop_extension(DesktopSurface::Settings);
  if (!install_desktop_surface(shell,DesktopSurface::Settings,settings)) return false;
  const bool startup_present=desktop_surface_extension(shell,DesktopSurface::Startup).open;
  const auto &startup=desktop_extension(DesktopSurface::Startup);
  if (install_desktop_surface(shell,DesktopSurface::Startup,startup)) {
    const bool data_present=desktop_surface_extension(shell,DesktopSurface::ApplicationData).open;
    if(install_desktop_surface(shell,DesktopSurface::ApplicationData,application_data_extension())) {
      const bool wifi_present=desktop_surface_extension(shell,DesktopSurface::Wifi).open;
      if(install_desktop_surface(shell,DesktopSurface::Wifi,desktop_extension(DesktopSurface::Wifi))) {
        if(desktop::notifications::install_surface(shell))return true;
        if(!wifi_present)remove_desktop_surface(shell,DesktopSurface::Wifi,desktop_extension(DesktopSurface::Wifi));
      }
      if(!data_present)remove_desktop_surface(shell,DesktopSurface::ApplicationData,application_data_extension());
    }
    if(!startup_present)remove_desktop_surface(shell,DesktopSurface::Startup,startup);
  }
  if (!present) remove_desktop_surface(shell,DesktopSurface::Settings,settings);
  return false;
}
}
