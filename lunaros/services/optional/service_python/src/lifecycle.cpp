#include "lunaros_micropython_lifecycle.h"
#include "lunaros_micropython_app_backend.h"
#include "kernel/service_executor.h"
#include "lunaros_micropython_runtime.h"
namespace lunaredu::micropython {
bool runtime_maintenance_step(void *,bool entering,uint32_t,std::string &error){
  if(!entering){error.clear();return true;}
  micropython_stop("firmware_update");micropython_repl_leave();
  const auto status=micropython_runtime_status();
  if(status.running||status.repl_active){error="python_retirement_pending";return false;}
  error.clear();return true;
}
namespace {
lunaros::ServiceStartResult start(void *p){
 if(!core_enabled())return {lunaros::ServiceStartDisposition::Ready,"explicitly_disabled"};
 if(!register_application_backend())return {lunaros::ServiceStartDisposition::Failed,"python_backend_registration_failed"};
 auto &config=*static_cast<RuntimeConfig *>(p);std::string error;
 const bool ready=configure_runtime(config,error);
 return {ready?lunaros::ServiceStartDisposition::Ready:lunaros::ServiceStartDisposition::Failed,ready?(config.start_on_demand?"vm_on_demand":"vm_ready"):(error.empty()?"vm_initialization_failed":error)};
}
bool stop(void *p,std::string &error){return stop_configured_runtime(*static_cast<RuntimeConfig *>(p),error);}
}
lunaros::ServiceNode runtime_lifecycle_node(lunaros::DeviceApiContext &device,RuntimeConfig &config,const lunaros::ServiceDependency *dependencies,size_t count,lunaros::ServiceCriticality criticality){
 config.owner_device=&device;return {{kRuntimeServiceId,"MicroPython Runtime",lunaros::kMainServiceExecutor,criticality,dependencies,count},start,stop,&config};
}
}
