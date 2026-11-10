#include "app/lifecycle/hosted_application.h"

namespace lunaredu::app {
bool HostedApplication::declare(AppController &controller,const char *id,ForegroundManifest manifest,
      HostedApplicationPorts ports,const ApplicationPreparation *preparation,std::string &error) {
    if(declared_) { error="hosted_application_already_declared";return false; }
    id_=id;ports_=ports;ForegroundDefinition definition;
    definition.id=id;definition.manifest=manifest;definition.context=this;definition.preparation=preparation;
    definition.prepare=[](void *p,std::string &e) {
      auto &self=*static_cast<HostedApplication *>(p);
      return !self.ports_.prepare||self.ports_.prepare(self.ports_.context,self.id_,e);
    };
    definition.start=[](void *p, lunaredu::app::ApplicationEnterReason,std::string &e) {
      auto &self=*static_cast<HostedApplication *>(p);
      return !self.ports_.enter||self.ports_.enter(self.ports_.context,self.id_,e)?
          ApplicationStartResult::Started:ApplicationStartResult::Failed;
    };
    definition.stop_checked=[](void *p,const char *,std::string &e) {
      auto &self=*static_cast<HostedApplication *>(p);
      return !self.ports_.leave||self.ports_.leave(self.ports_.context,self.id_,e);
    };
    declared_=controller.register_foreground(definition,error);return declared_;
  }
}
