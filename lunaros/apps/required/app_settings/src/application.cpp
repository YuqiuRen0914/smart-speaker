#include "lunaros_settings_application.h"
#include "desktop_session.h"

namespace lunaredu::settings {
bool Application::declare(app::AppController &controller,ApplicationPorts ports,std::string &error) {
#if defined(LUNAROS_SETTINGS_HEADLESS)
  if(ports.requires_display){error="headless_application_requires_display";return false;}
#endif
  if(declared_){error="settings_application_already_declared";return false;}
  if(ports.surface&&ports.shell){error="settings_surface_owner_conflict";return false;}
  surface_=ports.surface;
  if(ports.shell){surface_=make_desktop_session(ports,error);if(!surface_)return false;}
  ports_=ports;
  static constexpr lunaros::ResourceRequirement resources[]={{"display.main",lunaros::ResourceAccess::Exclusive}};
  app::ForegroundDefinition definition;
  definition.id=kApplicationId;definition.manifest.title="设置";
  definition.manifest.kind=app::ForegroundKind::Application;
  if(ports.requires_display){definition.manifest.resources=resources;definition.manifest.resource_count=1;}
  definition.context=this;
  definition.prepare=[](void *p,std::string &failure){
    auto &owner=*static_cast<Application *>(p);
    return !owner.ports_.prepare||owner.ports_.prepare(owner.ports_.context,kApplicationId,failure);
  };
  definition.start=[](void *p,app::ApplicationEnterReason,std::string &failure){
    auto &owner=*static_cast<Application *>(p);
    if(owner.surface_&&!owner.surface_->enter(failure))return app::ApplicationStartResult::Failed;
    return !owner.ports_.enter||owner.ports_.enter(owner.ports_.context,kApplicationId,failure)?app::ApplicationStartResult::Started:app::ApplicationStartResult::Failed;
  };
  definition.stop_checked=[](void *p,const char *,std::string &failure){
    auto &owner=*static_cast<Application *>(p);
    if(owner.surface_&&!owner.surface_->retire(failure))return false;
    if(owner.ports_.leave&&!owner.ports_.leave(owner.ports_.context,kApplicationId,failure))return false;
    if(owner.surface_)owner.surface_->finish();
    return true;
  };
  declared_=controller.register_foreground(definition,error);
  if(!declared_&&surface_){surface_->rollback();surface_.reset();}
  return declared_;
}
}
