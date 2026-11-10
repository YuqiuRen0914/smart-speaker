#include "desktop/shell/application.h"

namespace lunaredu::desktop {
bool ShellApplication::declare(app::AppController &controller,const char *id,const char *title,
    app::HostedApplicationPorts ports,std::string &error) {
    static constexpr lunaros::ResourceRequirement resources[]={{"display.main",lunaros::ResourceAccess::Exclusive}};
    app::ForegroundManifest manifest;manifest.kind=app::ForegroundKind::SystemSurface;
    manifest.title=title;manifest.resources=resources;manifest.resource_count=1;manifest.is_home=true;
    return application_.declare(controller,id,manifest,ports,nullptr,error);
  }
} // namespace lunaredu::desktop
