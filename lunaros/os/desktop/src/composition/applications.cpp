#include "composition/applications_internal.h"
#include "desktop/composition/applications.h"
#include "desktop/shell/application.h"
#include "desktop/application/downloaded_host.h"
#include <array>
#include <cstring>

namespace lunaredu::lunaros {
struct DesktopApplications::Impl {
  desktop::ShellApplication shell;
  apps::DownloadedHost downloaded;
  std::array<std::unique_ptr<DesktopAppInstance>,app::kApplicationCapacity> applications;
  size_t count=0;
};
DesktopApplications::DesktopApplications()=default;
DesktopApplications::~DesktopApplications()=default;
bool DesktopApplications::declare(app::AppController &controller,const char *shell_id,
    const desktop::DesktopApplication *entries,size_t count,DesktopApplicationPorts ports,std::string &error) {
  if(impl_){error="desktop_applications_already_attempted";return false;}
  if(!entries||!count||count>app::kApplicationCapacity||!shell_id||!*shell_id||
      (ports.provider_count&&!ports.providers)||ports.provider_count>app::kApplicationCapacity){
    error="invalid_desktop_applications";return false;
  }
  for(size_t i=0;i<ports.provider_count;++i){
    const auto &provider=ports.providers[i];
    if(!provider.id||!*provider.id||!provider.create||!std::strcmp(provider.id,shell_id)||!std::strcmp(provider.id,apps::kDownloadedHostId)){
      error="invalid_desktop_application_provider";return false;
    }
    for(size_t j=0;j<i;++j)if(!std::strcmp(provider.id,ports.providers[j].id)){
      error="duplicate_desktop_application_provider:"+std::string(provider.id);return false;
    }
  }
  const desktop::DesktopApplication *shell=nullptr;
  for(size_t i=0;i<count;++i){
    if(!entries[i].id||!*entries[i].id||!entries[i].name||!*entries[i].name){error="invalid_desktop_application";return false;}
    for(size_t j=0;j<i;++j)if(!std::strcmp(entries[i].id,entries[j].id)){error="duplicate_desktop_application:"+std::string(entries[i].id);return false;}
    if(!std::strcmp(entries[i].id,shell_id))shell=&entries[i];
    if(!std::strcmp(entries[i].id,apps::kDownloadedHostId)&&(!ports.open_guest||!ports.close_guest)){
      error="downloaded_backend_unavailable";return false;
    }
  }
  if(!shell){error="desktop_shell_entry_missing";return false;}
  impl_.reset(new(std::nothrow) Impl);
  if(!impl_){error="desktop_application_host_allocation_failed";return false;}
  const app::HostedApplicationPorts hosted{ports.context,ports.prepare,ports.enter,ports.leave};
  if(!impl_->shell.declare(controller,shell_id,shell->name,hosted,error))return false;
  for(size_t i=0;i<count;++i){
    const auto *id=entries[i].id;
    if(!std::strcmp(id,apps::kDownloadedHostId)){
      if(!impl_->downloaded.declare(controller,shell_id,{ports.context,ports.prepare,ports.open_guest,ports.close_guest,
          ports.enter,ports.leave,ports.package_preparation,ports.package_service_id},error))return false;
    }else if(const auto *provider=find_desktop_application(ports.providers,ports.provider_count,id)){
      auto &instance=impl_->applications[impl_->count++];instance=provider->create();
      if(!instance){error="desktop_application_allocation_failed:"+std::string(id);return false;}
      if(!instance->declare(controller,ports,error))return false;
    }
  }
  error.clear();return true;
}
bool DesktopApplications::open_guest(const std::string &id,std::string &error) {
  if(!impl_){error="desktop_applications_unavailable";return false;}
  return impl_->downloaded.open(id,error);
}
}
