#include "shell/maintenance.h"
#include <cstring>

namespace lunaredu::desktop {
bool HomeUninstallOperation::request(DesktopShell &shell,const lunaros::apps::ApplicationDataPorts &ports,std::string &error){
  if(receipt){error="application_operation_pending";return false;}
  auto &home=shell.home;auto &compact=shell.compact;id=home.enabled?home.pending_uninstall:compact.pending_uninstall;
  const auto index=shell.find_app(id.c_str());
  if((!home.enabled&&!compact.enabled)||id.empty()||index>=shell.count||!shell.apps[index].uninstallable){
    error="desktop_application_uninstall_unavailable";return false;
  }
  if((ports.prepare&&!ports.prepare(ports.prepare_context,error))||!ports.uninstall||
     !ports.uninstall(ports.context,id,false,receipt,error))return false;
  if(!receipt){error="application_operation_receipt_missing";return false;}
  error.clear();return true;
}
bool HomeUninstallOperation::poll(DesktopShell &shell,const lunaros::apps::ApplicationDataPorts &ports,std::string &error){
  using lunaros::apps::ApplicationOperationPhase;
  if(!receipt||receipt.pending())return false;
  const auto result=receipt.result();uint64_t revision=0;
  if(result.phase!=ApplicationOperationPhase::Succeeded){error=result.error;receipt={};return true;}
  if(!ports.observe_revision||!ports.observe_revision(ports.context,revision)){
    error="application_catalog_observation_unavailable";receipt={};return true;
  }
  if(revision<result.catalog_revision)return false;
  receipt={};
  lunaros::apps::ApplicationList catalog;
  if(!ports.catalog||!ports.catalog(ports.context,catalog,error)){
    error="application_catalog_observation_unavailable";return true;
  }
  for(const auto &app:catalog)if(app.id==id){error="installed_catalog_changed";return true;}
  auto &home=shell.home;auto &compact=shell.compact;
  if(home.layout)for(size_t i=0;i<home.layout->items.size();++i)
    if(home.layout->items[i].kind==HomeItemKind::Application&&home.layout->items[i].id==id){home.remove(i,false);break;}
  if(compact.enabled)compact.hide(shell,id.c_str());
  if(!std::strcmp(shell.preferences.startup_id,id.c_str())){shell.preferences.startup_id[0]=0;shell.preferences_changed=true;}
  home.pending_uninstall.clear();home.error.clear();compact.pending_uninstall.clear();compact.error.clear();error.clear();return true;
}
}
