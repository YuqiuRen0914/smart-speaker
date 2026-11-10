#include "desktop/composition/native_application.h"
namespace lunaredu::desktop {
app::ForegroundDefinition NativeApplicationBinding::definition() {
    auto result=application_; result.context=this;
    if(application_.prepare)result.prepare=[](void *p,std::string &e){auto &s=self(p);return s.application_.prepare(s.application_.context,e);};
    result.start=[](void *p,app::ApplicationEnterReason reason,std::string &error){
      auto &s=self(p);
      if(!s.application_.start||!s.port_.entered){error="native_application_binding_invalid";return app::ApplicationStartResult::Failed;}
      const auto started=s.application_.start(s.application_.context,reason,error);
      if(started!=app::ApplicationStartResult::Started)return started;
      if(!s.port_.entered(s.port_.context,s.route_)){error="native_desktop_entry_rejected";return app::ApplicationStartResult::Failed;}
      return started;
    };
    if(application_.tick)result.tick=[](void *p){auto &s=self(p);s.application_.tick(s.application_.context);};
    if(application_.capability_tick)result.capability_tick=[](void *p){auto &s=self(p);s.application_.capability_tick(s.application_.context);};
    if(application_.stop)result.stop=[](void *p,const char *reason){auto &s=self(p);s.application_.stop(s.application_.context,reason);};
    if(application_.stop_checked)result.stop_checked=[](void *p,const char *reason,std::string &e){auto &s=self(p);return s.application_.stop_checked(s.application_.context,reason,e);};
    return result;
  }
}
