#pragma once
#include "desktop/ui/entry.h"
#include "app/lifecycle/hosted_application.h"
namespace lunaredu::desktop {
inline UiEntry shell_ui_entry() { return {kShellView,UiEntryKind::Home}; }
class ShellApplication {
 public:
  bool declare(app::AppController &controller,const char *id,const char *title,
      app::HostedApplicationPorts ports,std::string &error);
 private:
  app::HostedApplication application_;
};
}
