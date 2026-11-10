#include "lunaredu_music_desktop.h"

namespace lunaredu::music {
bool Application::declare(app::AppController &c,lunaros::DesktopApplicationPorts p,std::string &e) {
    static constexpr lunaros::ResourceRequirement r[]={{"display.main",lunaros::ResourceAccess::Exclusive}};
    configure_player(c.runtime());ports_=p;
    app::ForegroundManifest m;m.title="音乐";m.icon="music";m.resources=r;m.resource_count=1;
    return owner_.declare(c,kApplicationId,m,{this,
      [](void *ctx,const char *id,std::string &error){auto &p=static_cast<Application *>(ctx)->ports_;return !p.prepare||p.prepare(p.context,id,error);},
      [](void *ctx,const char *id,std::string &error){auto &p=static_cast<Application *>(ctx)->ports_;return !p.enter||p.enter(p.context,id,error);},
      [](void *ctx,const char *id,std::string &error){if(!leave_local(error))return false;auto &p=static_cast<Application *>(ctx)->ports_;return !p.leave||p.leave(p.context,id,error);}
    },nullptr,e);
  }
}
