#pragma once
#include "desktop/ui/entry.h"
#include "lunaros_python_ui_snapshot.h"
#include "lunaros_python_application.h"
#include "desktop/ui/host_port.h"
#include "app/execution/application_context.h"
#include "app/lifecycle/app_controller.h"
namespace lunaredu::python {
// The App retains presentation and performs checked VM retirement. A product
// binds the same logical UI port used by other Apps and chooses its return route.
class DesktopSession {
 public:
  explicit DesktopSession(desktop::UiHostPort &port):port_(port){}
  DesktopSession(const DesktopSession &)=delete;
  DesktopSession &operator=(const DesktopSession &)=delete;
  void configure(lunaros::ApplicationContext *device,const char *shell) { device_=device;shell_id_=shell; }
  const Presentation &view() const { return shown_.python_view; }
  void request_exit(bool home) { exit_pending_=true; exit_home_=exit_home_||home; }
  bool launch(const char *id,std::string &error);
  bool project_foreground(bool active,app::ViewRoute return_route);
  void tick();
 private:
  desktop::UiHostPort &port_;
  UiState shown_;
  lunaros::ApplicationContext *device_=nullptr;
  const char *shell_id_=nullptr;
  bool exit_pending_=false,exit_home_=false;
};
}
