#pragma once
#include "desktop/composition/applications.h"
#include "desktop/composition/selection.h"
#include "lunaros_files_application.h"
#include "lunaros_files_ui.h"
#include "lunaros_files_extension.h"
namespace lunaredu::files {
struct DesktopRegistration {
  bool operator()(Application &application,app::AppController &controller,lunaros::DesktopApplicationPorts ports,std::string &error) const {
    return application.declare(controller,{ports.context,ports.prepare,ports.enter,ports.leave,ports.shell,ports.now_ms},error);
  }
};
inline lunaros::DesktopAppDescriptor desktop_registration() {
  return {kApplicationId,lunaros::DesktopAppOwner<Application,DesktopRegistration>::create,nullptr,nullptr,
      [](desktop::DesktopShell &shell,uint32_t now,bool visible){return desktop::update_desktop_files(shell,now,visible);}};
}
struct DesktopEntry : lunaros::DesktopEntryDefaults {
  static auto tile() { return desktop_application(); }
  static auto provider() { return desktop_registration(); }
  template<class Bindings> static auto binding() { return ui_binding<Bindings>(); }
};
}
