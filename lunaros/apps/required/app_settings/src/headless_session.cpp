#include "desktop_session.h"
namespace lunaredu::settings {
std::shared_ptr<app::ApplicationSurfaceSession> make_desktop_session(const ApplicationPorts &,std::string &error) {
  error="settings_desktop_not_built";return {};
}
}
