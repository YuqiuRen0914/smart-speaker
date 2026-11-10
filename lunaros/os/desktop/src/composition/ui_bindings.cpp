#include "composition/applications_internal.h"
#include "desktop/composition/applications.h"
#include "desktop/composition/composition.h"
#include "desktop/composition/shell_observers.h"
#include "desktop/ui/host.h"

namespace lunaredu::lunaros {
desktop::UiHostState &DesktopCompositionAccess::state(desktop::DesktopUiHost &ui) {return ui.state_;}
void DesktopApplicationBindings::bind_maintenance(apps::ApplicationDataPorts ports){ui_.bind_home_maintenance(ports);}
bool prepare_desktop_shell(desktop::DesktopUiHost &ui,DesktopRange<desktop::DesktopApplication> tiles,
    DesktopRange<desktop::DesktopMenu> menus,DesktopPreparation options,std::string &error) {
  if(!options.bindings_valid){error="desktop_ui_bindings_invalid";return false;}
  auto &shell=DesktopCompositionAccess::state(ui).desktop;
  if(!shell.configure(tiles.data(),tiles.size())||(menus.size()&&!shell.configure_menus(menus.data(),menus.size()))){
    error="desktop_shell_configuration_failed";return false;
  }
  if(!options.display_ready){error="desktop_display_unavailable";return false;}
  return true;
}
bool start_desktop_foreground(desktop::DesktopUiHost &ui,ApplicationContext *device,std::string &error) {
  return ui.session().start([&](std::string &failure){
    if(!device||!device->application()){failure="desktop_application_owner_missing";return false;}
    return device->application()->setup(ui.foreground(DesktopCompositionAccess::state(ui).page),failure);
  },error);
}
bool DesktopApplicationBindings::enter(const char *id,std::string &error) {
  if(!id||!*id){error="desktop_application_id_missing";return false;}
  if(admit_package_&&!admit_package_(package_context_,id,error))return false;
  if(product_.enter&&!product_.enter(product_.context,id,error))return false;
  ui_.project_foreground(id);return true;
}
DesktopApplicationPorts DesktopApplicationBindings::ports(app::HostedApplicationPorts product,uint32_t (*clock)()) {
  product_=product;package_context_=nullptr;admit_package_=nullptr;
  DesktopApplicationPorts ports;
  ports.context=this;ports.shell=&DesktopCompositionAccess::state(ui_).desktop;ports.now_ms=clock;
  ports.prepare=[](void *p,const char *id,std::string &e){
    auto &self=*static_cast<DesktopApplicationBindings *>(p);
    return !self.product_.prepare||self.product_.prepare(self.product_.context,id,e);
  };
  ports.enter=[](void *p,const char *id,std::string &e){return static_cast<DesktopApplicationBindings *>(p)->enter(id,e);};
  ports.leave=[](void *p,const char *id,std::string &e){
    auto &self=*static_cast<DesktopApplicationBindings *>(p);
    if(self.product_.leave)return self.product_.leave(self.product_.context,id,e);
    e.clear();return true;
  };
  return ports;
}
bool DesktopShellObservers::configure(KernelRuntime &runtime,const DesktopAppDescriptor *providers,size_t count,
    const desktop::DesktopApplication *selected,size_t selected_count,std::string &error) {
  count_=0;
  for(size_t i=0;i<selected_count;++i){
    const auto *provider=find_desktop_application(providers,count,selected[i].id);
    if(!provider)continue;
    if(provider->prepare&&!provider->prepare(error))return false;
    if(provider->configure)provider->configure(runtime);
    if(provider->poll){
      if(count_==providers_.size()){error="desktop_observer_capacity";return false;}
      providers_[count_++]=provider;
    }
  }
  return true;
}
bool DesktopShellObservers::poll(desktop::DesktopShell &shell,uint32_t now,bool visible) {
  bool changed=false;
  for(size_t i=0;i<count_;++i)changed=providers_[i]->poll(shell,now,visible)||changed;
  return changed;
}
}
