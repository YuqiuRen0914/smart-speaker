#include "lunaros_python_desktop_session.h"

namespace lunaredu::python {
bool DesktopSession::launch(const char *id,std::string &error) {
    if(!device_||!device_->application()){error="python_application_owner_missing";return false;}
    return device_->application()->switch_to(id,error);
  }

bool DesktopSession::project_foreground(bool active,app::ViewRoute return_route) {
    if(active){shown_.python_view={};exit_pending_=exit_home_=false;return port_.application_entered({kDesktopView});}
    exit_pending_=exit_home_=false;
    return port_.application_entered({return_route});
  }

void DesktopSession::tick() {
    if(!port_.ready_ui()||port_.ui_route()!=kDesktopView)return;
    desktop::DesktopCommand command{};const bool pending=port_.take_command(command);
    if(pending&&(command==desktop::DesktopCommand::Home||command==desktop::DesktopCommand::Back))request_exit(command==desktop::DesktopCommand::Home);
    if(exit_pending_){
      const bool home=exit_home_;
      std::string error;
      const bool stopped=home?launch(shell_id_,error):device_&&SystemApplication::leave_foreground(*device_,error);
      if(!stopped){
        port_.log_ui("python",std::string("Python exit pending: ")+error,desktop::UiLogLevel::Warning);return;
      }
      if(home)port_.home();
      return;
    }
    const auto current=presentation();
    if(port_.take_ui_redraw()||current!=shown_.python_view){
      shown_.python_view=current;port_.render_ui();
    }
  }
}
