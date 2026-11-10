#pragma once
#include "lunaros_files_application.h"
namespace lunaredu::files {
std::shared_ptr<app::ApplicationSurfaceSession> make_desktop_session(const ApplicationPorts &,std::string &);
}
