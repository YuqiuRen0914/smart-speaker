#include "desktop_session.h"
#include "desktop/shell/notifications.h"
#include "lunaros_settings_wifi_runtime.h"
#include "desktop/shell/shell.h"
#include "lunaros_settings_menu.h"
#include "lunaros_settings_application_data.h"
namespace lunaredu::settings {
namespace {
class DesktopSession final:public app::ApplicationSurfaceSession {
  desktop::DesktopShell &shell_;
  uint32_t (*clock_)();
  lunaros::apps::ApplicationDataPorts data_;
  bool settings_,startup_,wifi_,notices_,data_present_;
public:
  explicit DesktopSession(const ApplicationPorts &ports):shell_(*ports.shell),clock_(ports.now_ms),data_(ports.data),
    settings_(desktop::desktop_surface_extension(shell_,desktop::DesktopSurface::Settings).open),
    startup_(desktop::desktop_surface_extension(shell_,desktop::DesktopSurface::Startup).open),
    wifi_(desktop::desktop_surface_extension(shell_,desktop::DesktopSurface::Wifi).open),
    notices_(desktop::desktop_surface_extension(shell_,desktop::DesktopSurface::Notifications).open),
    data_present_(desktop::desktop_surface_extension(shell_,desktop::DesktopSurface::ApplicationData).open){}
  bool install(){return install_desktop_surfaces(shell_);}
  bool enter(std::string &error)override{bind_application_data(shell_,data_);error.clear();return true;}
  bool retire(std::string &error)override {
    (void)desktop::update_desktop_wifi(shell_,clock_?clock_():shell_.now_ms,false);error.clear();return true;
  }
  void finish()override {
    if(shell_.surface==desktop::DesktopSurface::Notifications)desktop::notifications::extension().close(shell_);
  }
  void rollback()override {
    if(!notices_)desktop::notifications::remove_surface(shell_);
    if(!wifi_)desktop::remove_desktop_surface(shell_,desktop::DesktopSurface::Wifi,desktop_extension(desktop::DesktopSurface::Wifi));
    if(!settings_)desktop::remove_desktop_surface(shell_,desktop::DesktopSurface::Settings,desktop_extension(desktop::DesktopSurface::Settings));
    if(!startup_)desktop::remove_desktop_surface(shell_,desktop::DesktopSurface::Startup,desktop_extension(desktop::DesktopSurface::Startup));
    if(!data_present_)desktop::remove_desktop_surface(shell_,desktop::DesktopSurface::ApplicationData,application_data_extension());
  }
};
}
std::shared_ptr<app::ApplicationSurfaceSession> make_desktop_session(const ApplicationPorts &ports,std::string &error) {
  if(!ports.shell)return {};
  auto session=std::make_shared<DesktopSession>(ports);
  if(!session->install()){session->rollback();error="settings_surface_already_owned";return {};}
  error.clear();return session;
}
}
