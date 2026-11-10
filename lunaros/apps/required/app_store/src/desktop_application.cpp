#include "lunaros_app_manager_application.h"

namespace lunaredu::lunaros::apps::manager {
bool SystemApplication::declare(app::AppController &controller,app::HostedApplicationPorts ports,
    const app::ApplicationPreparation *preparation,std::string &error) {
    static constexpr ResourceRequirement resources[]={{"display.main",ResourceAccess::Exclusive}};
    static constexpr app::ApplicationServiceRequirement services[]={app::required_service(kApplicationsServiceId)};
    app::ForegroundManifest manifest;manifest.title="应用商店";
    manifest.services=services;manifest.service_count=std::size(services);
    manifest.resources=resources;manifest.resource_count=1;
    return application_.declare(controller,kApplicationId,manifest,ports,preparation,error);
  }
} // namespace lunaredu::lunaros::apps::manager
