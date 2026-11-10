#include "desktop_session.h"
#include "lunaros_files_extension.h"
namespace lunaredu::files {
namespace {
class DesktopSession final:public app::ApplicationSurfaceSession {
  desktop::DesktopShell &shell_;
  uint32_t (*clock_)();
  std::shared_ptr<Workspace> workspace_;
public:
  DesktopSession(desktop::DesktopShell &shell,uint32_t(*clock)()):shell_(shell),clock_(clock),workspace_(std::make_shared<Workspace>()){}
  bool install(){return desktop::install_file_surface(shell_,workspace_);}
  bool retire(std::string &error)override {
    (void)desktop::update_desktop_files(shell_,clock_?clock_():shell_.now_ms,false);
    if(!workspace_->retire()||workspace_->cleanup_pending()) {error="file_player_stop_pending";return false;}
    if(const auto view=desktop::file_browser(shell_);view&&view->playback.token){error="file_player_stop_pending";return false;}
    error.clear();return true;
  }
  void rollback()override{desktop::remove_file_surface(shell_,workspace_);}
};
}
std::shared_ptr<app::ApplicationSurfaceSession> make_desktop_session(const ApplicationPorts &ports,std::string &error) {
  if(!ports.shell)return {};
  auto session=std::make_shared<DesktopSession>(*ports.shell,ports.now_ms);
  if(!session->install()){error="files_surface_already_owned";return {};}
  error.clear();return session;
}
}
