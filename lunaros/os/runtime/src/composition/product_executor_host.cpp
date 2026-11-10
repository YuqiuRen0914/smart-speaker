#include "runtime/product_executor_host.h"
#include "runtime/update_maintenance.h"
#include <vector>

namespace lunaredu::lunaros {
bool ProductExecutorHost::start(std::initializer_list<ServiceExecutorDefinition> executors,
    const NetworkPolicy &network, const char *timezone, ServiceInstaller install,
    std::initializer_list<ExecutorTaskSpec> tasks, RuntimeBootstrapStage *failed_stage,
    std::string &error, const lunaredu::lunaros::ProvisioningPolicy *provisioning,
    std::span<const ModuleMaintenance> maintenance,std::span<const RuntimeHealthProbe> health) {
    if (!install) {
      if (!admit_start(error)) return false;
      error = "product_service_installer_missing";
      if (failed_stage) *failed_stage = RuntimeBootstrapStage::ServiceRegistration;
      return false;
    }
    struct Startup {
      ProductExecutorHost &host;
      ServiceInstaller install;
      std::span<const ExecutorTaskSpec> tasks;
    } startup{*this, install, {tasks.begin(),tasks.size()}};
    // bootstrap invokes both hooks synchronously. ExecutorTaskGroup copies
    // task specs before returning; no task retains this stack-local context.
    return ProductRuntime::start(executors, network, timezone,
        {.provisioning=provisioning, .register_services = [](void *opaque, std::string &failure) {
           auto &s = *static_cast<Startup *>(opaque);
           return s.install(s.host, failure);
         },
         .start_executor_tasks = [](void *opaque, std::string &failure) {
           auto &s = *static_cast<Startup *>(opaque);
           // Build after ProductRuntime's one-shot admission. Core copies each
           // spec; neither it nor a repeated start borrows this temporary plan.
           std::vector<ExecutorTaskSpec> plan(s.tasks.begin(),s.tasks.end());
           for(auto &task:plan){
             task.checkpoint=[](void *p,const char *executor){poll_update_maintenance(*static_cast<DeviceContext *>(p),executor);};
             task.checkpoint_context=&s.host.context();
           }
           return s.host.tasks_.start(plan, failure);
         },
         .context = &startup, .health=health, .module_maintenance=maintenance}, failed_stage, error);
}
}
