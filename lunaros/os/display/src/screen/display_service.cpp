#include "display/display_service.h"
#include "kernel/service_executor.h"
#include "kernel/runtime.h"

namespace lunaredu::display {
DisplayService::DisplayService(lunaros::KernelRuntime &runtime, const char *id,
                               ScreenServicePorts ports)
    : runtime_(runtime), id_(id), ports_(ports) {}

bool DisplayService::healthy(){
  const bool ready=runtime_.service_available(id_)&&ports_.ready&&ports_.ready(ports_.context);
  if(health_.has_completed_transfer())health_.observe_ready(ready);
  else health_.observe(ready,ports_.completed_transfers?ports_.completed_transfers(ports_.context):0);
  return health_.healthy();
}
lunaros::ServiceNode DisplayService::node() {
  return {{id_, "Display", lunaros::kMainServiceExecutor,
      lunaros::ServiceCriticality::Optional, ports_.dependencies, ports_.dependency_count},
    [](void *p) { return static_cast<DisplayService *>(p)->start(); },
    [](void *p, std::string &error) { return static_cast<DisplayService *>(p)->stop(error); }, this};
}

bool DisplayService::stop(std::string &error) {
  if (!attempted_ || stopped_) {error.clear();return true;}
  if (ports_.stop) {if(!ports_.stop(ports_.context,error))return false;}
  else if (!ports_.retained || ports_.retained(ports_.context)) {error="display_stop_unavailable";return false;}
  else if(result_.disposition==lunaros::ServiceStartDisposition::Ready) {error="display_stop_unavailable";return false;}
  stopped_=true;error.clear();return true;
}
lunaros::ServiceStartResult DisplayService::start() {
  if (stopped_) return {lunaros::ServiceStartDisposition::Failed,"display_stopped"};
  if (attempted_) return result_;
  attempted_ = true;
  std::string error;
  if (ports_.prepare && ports_.prepare(ports_.context, runtime_, error))
    return result_ = {lunaros::ServiceStartDisposition::Ready, "resident_pipeline_ready"};
  if (error.empty()) error = "display_prepare_failed";
  return result_ = {lunaros::ServiceStartDisposition::Failed, error};
}
} // namespace lunaredu::display
