#include "desktop_session.h"
namespace lunaredu::files {
std::shared_ptr<app::ApplicationSurfaceSession> make_desktop_session(const ApplicationPorts &,std::string &error) {
  error="files_desktop_not_built";return {};
}
}
