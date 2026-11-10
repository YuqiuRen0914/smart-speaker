#pragma once
#include "lunaros_settings_application.h"
namespace lunaredu::settings {
std::shared_ptr<app::ApplicationSurfaceSession> make_desktop_session(const ApplicationPorts &,std::string &);
}
