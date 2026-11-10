#include "device_api/bootstrap_metrics_json.h"
namespace lunaredu::lunaros {

void fill_bootstrap_metrics_payload(const DeviceApiContext &device, JsonObject payload) {
  const auto timing=device.bootstrap_metrics();
  payload["available"]=timing.available;
  payload["completed"]=timing.completed;
  payload["succeeded"]=timing.succeeded;
  if (!timing.available) return;
  payload["total_ms"]=timing.total_ms;
  payload["scope"]="os_bootstrap_to_services_started";
  auto stages=payload["stage_ms"].to<JsonObject>();
  for(size_t i=0;i<timing.stage_ms.size();++i)stages[kBootstrapStageNames[i]]=timing.stage_ms[i];
}
}
