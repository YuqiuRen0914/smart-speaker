#include "device_api/operation_feedback.h"
#include "diagnostics/trace.h"
#include <cstdio>
namespace lunaredu::lunaros {
DeviceOperationCorrelation begin_device_operation_feedback(DeviceApiContext &device) {
  DeviceOperationCorrelation correlation;
  correlation.request_sequence = device.model.allocate_request_sequence();
  return correlation;
}

void fill_device_operation_correlation(JsonObject response,
                                              const DeviceOperationCorrelation &correlation) {
  if (correlation.request_sequence) {
    char request[32];
    format_device_request_id(request, sizeof(request), correlation.request_sequence);
    response["request_id"] = request;
  }
  if (correlation.trace_id) {
    char trace[21];
    std::snprintf(trace, sizeof(trace), "%llu", static_cast<unsigned long long>(correlation.trace_id));
    response["trace_id"] = trace;
    response["trace_generation"] = correlation.trace_generation;
  } else response["trace_id"] = nullptr;
}

void finish_device_operation_feedback(DeviceApiContext &device, const DeviceModuleSpec &module,
    const DeviceOperationSpec &operation, const DeviceOperationResult &result,
    JsonObject data, JsonObject response, const DeviceOperationCorrelation &correlation) {
  fill_device_operation_correlation(response, correlation);
  response["completion"]=operation_completion_name(result.completion);
  // Legacy handlers may only supply their existing error code; do not infer a
  // domain category from HTTP status or text.
  if(result.category!=OperationError::None)response["error_category"]=operation_error_name(result.category);
  if (!operation.mutating) return;
  if((result.completion==OperationCompletion::Unknown||result.completion==OperationCompletion::Accepted) &&
      !data["activity_id"].is<const char *>()) {
    // A timed-out foreign write can still run. Creating a terminal failed
    // activity here would contradict the actual owner and invite a replay.
    response["activity_correlation"]="unavailable";
    return;
  }
  if (!data["activity_id"].is<const char *>()) {
    const auto id = device.model.activities.accept(module.capability_id, operation.id,
        result.ok ? "operation accepted" : "operation failed");
    if(result.completion!=OperationCompletion::NotStarted)
      (void)device.model.activities.start(id.c_str(), "executing device operation");
    if (result.ok) {
      std::string saved;
      serializeJson(data, saved);
      (void)device.model.activities.succeed(id.c_str(), saved.c_str(), "operation completed");
    } else {
      const char *code = result.error.empty() ? "operation_failed" : result.error.c_str();
      (void)device.model.activities.fail(id.c_str(), code, code);
    }
    data["activity_id"] = id;
    data["activity_state"] = result.ok ? "succeeded" : "failed";
  }
  const char *id = data["activity_id"].as<const char *>();
  const bool linked = device.model.activities.correlate(id, module.capability_id, operation.id,
      correlation.request_sequence, correlation.trace_id);
  response["activity_correlation"] = linked ? "linked" : "unavailable";
#if CONFIG_LUNAREDU_SYSTEM_DIAGNOSTICS || CONFIG_LUNAREDU_LOCAL_DEBUG_SURFACE
  if (linked && correlation.trace_id) {
    char qualified[160];
    std::snprintf(qualified, sizeof(qualified), "%s.%s", module.id, operation.id);
    record_diagnostic_trace({correlation.trace_id, correlation.trace_generation,
        correlation.request_sequence}, DiagnosticPhase::ActivityLinked, qualified, id);
  }
#endif
}
}
