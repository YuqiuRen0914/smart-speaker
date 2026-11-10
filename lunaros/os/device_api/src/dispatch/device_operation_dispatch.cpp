#include "device_api/operation_dispatch.h"
#include "device_api/composed_operation.h"
#include "device_api/device_api_context.h"
#include "device_api/device_projection.h"
#include "diagnostics/trace.h"
#include "memory/json_workspace.h"
#include <cstdio>
#include <cstring>
#include <new>
#include <memory>
namespace lunaredu::lunaros {
bool dispatch_device_operation(KernelRuntime &runtime,
                                      const DeviceOperationSpec &model,
                                      ServiceOperation operation, void *context,
                                      std::string &error, const char *module ,
                                      DeviceOperationCorrelation *correlation ) {
  error.clear();
  (void)module;
  if (correlation) { correlation->trace_id = 0; correlation->trace_generation = 0; }
#if CONFIG_LUNAREDU_SYSTEM_DIAGNOSTICS || CONFIG_LUNAREDU_LOCAL_DEBUG_SURFACE
  char qualified[160] = {};
  const bool capture = diagnostic_capture_enabled();
  if (capture) std::snprintf(qualified, sizeof(qualified), "%s%s%s", module ? module : "",
                module ? "." : "", model.id ? model.id : "unknown");
  // Reading or toggling diagnostics must not generate its own capture traffic.
  const auto trace = capture && model.diagnostic_access == DeviceDiagnosticAccess::None
      ? begin_diagnostic_trace(qualified, correlation ? correlation->request_sequence : 0) : DiagnosticTrace{};
  if (correlation) { correlation->trace_id = trace.id; correlation->trace_generation = trace.generation; }
  struct Invocation { ServiceOperation call; void *context; DiagnosticTrace trace; const char *id; bool started = false; };
  Invocation invocation{operation, context, trace, qualified};
  const auto traced = [](void *opaque, ServiceOperationResult &status) {
    auto &call = *static_cast<Invocation *>(opaque);
    call.started = true;
    record_diagnostic_trace(call.trace, DiagnosticPhase::Started, call.id);
    call.call(call.context, status);
    const bool ok = status.ok && (!status.work_result_known || status.work_ok);
    record_diagnostic_trace(call.trace, ok ? DiagnosticPhase::Returned : DiagnosticPhase::HandlerFailed,
                            call.id, status.work_result_known ? status.work_error_code : status.error.c_str());
  };
#endif
  if (!device_operation_execution_valid(model) || operation == nullptr) {
    error = "invalid_device_operation_execution";
#if CONFIG_LUNAREDU_SYSTEM_DIAGNOSTICS || CONFIG_LUNAREDU_LOCAL_DEBUG_SURFACE
    record_diagnostic_trace(trace, DiagnosticPhase::DispatchFailed, qualified, error.c_str());
#endif
    return false;
  }
#if CONFIG_LUNAREDU_SYSTEM_DIAGNOSTICS || CONFIG_LUNAREDU_LOCAL_DEBUG_SURFACE
  if (trace.id) { operation = traced; context = &invocation; }
#endif
  if (model.execution == DeviceOperationExecution::Caller) {
    ServiceOperationResult status;
    operation(context, status);
    error = status.error;
    return status.ok;
  }
  const bool ok = runtime.invoke_executor(model.owner_executor, operation, context, error);
#if CONFIG_LUNAREDU_SYSTEM_DIAGNOSTICS || CONFIG_LUNAREDU_LOCAL_DEBUG_SURFACE
  if (!ok && !invocation.started) record_diagnostic_trace(trace, DiagnosticPhase::DispatchFailed, qualified, error.c_str());
#endif
  return ok;
}

namespace {
DeviceOperationResult resolve_composed(DeviceApiContext &device,
    const ComposedDeviceOperation &request,
    const DeviceOperationSpec *&model,
    const DeviceOperationProjection *&projection,JsonVariantConst input={}) {
  auto *registry=device.projections();
  if(!registry || !request.module || !request.operation || !request.capability || !request.caller_module)
    return {false,404,"composed_operation_not_found",OperationCompletion::NotStarted,OperationError::NotFound};
  if(!std::strcmp(request.module,request.caller_module))
    return {false,403,"composed_self_call_forbidden",OperationCompletion::NotStarted,OperationError::Forbidden};
  const auto *module=device.model.models.find_module(request.module);
  model=device.model.models.find_operation(request.module,request.operation);
  projection=registry->find_operation(request.module,request.operation);
  if(!module || !model || !projection)
    return {false,404,"composed_operation_not_found",OperationCompletion::NotStarted,OperationError::NotFound};
  if(std::strcmp(module->capability_id,request.capability))
    return {false,403,"composed_capability_mismatch",OperationCompletion::NotStarted,OperationError::Forbidden};
  if(model->diagnostic_access!=DeviceDiagnosticAccess::None)
    return {false,403,"composed_diagnostic_operation_forbidden",OperationCompletion::NotStarted,OperationError::Forbidden};
  const auto expected=request.command?DeviceRemoteAccess::Command:DeviceRemoteAccess::Query;
  if(!projection->composable && model->remote_access!=expected)
    return {false,403,"composed_operation_not_allowed",OperationCompletion::NotStarted,OperationError::Forbidden};
  if(!device.model.capabilities.active(module->capability_id))
    return {false,503,"capability_unavailable",OperationCompletion::NotStarted,OperationError::Unavailable};
  const bool kind_ok=request.command
      ? model->kind==DeviceOperationKind::Command && model->mutating &&
        projection->handler_kind==DeviceOperationHandlerKind::JsonCommand &&
        (projection->json_command_handler || projection->json_binding)
      : model->kind==DeviceOperationKind::Query && !model->mutating &&
        projection->handler_kind==DeviceOperationHandlerKind::JsonQuery &&
        (projection->json_provider || projection->json_result_provider || projection->json_binding);
  if(!kind_ok || !device_operation_execution_valid(*model))
    return {false,403,"composed_operation_kind_forbidden",OperationCompletion::NotStarted,OperationError::Forbidden};
  if(projection->maximum_request_bytes&&!input.isNull()&&
      measureJson(input)>projection->maximum_request_bytes)
    return {false,413,"composed_operation_payload_too_large",OperationCompletion::NotStarted,OperationError::InvalidArgument};
  return {};
}

void execute_composed(DeviceApiContext &device,const ComposedDeviceOperation &request,
    JsonVariantConst input,JsonObject output,DeviceOperationResult &outcome,ServiceOperationResult &status) {
  const DeviceOperationSpec *model=nullptr;
  const DeviceOperationProjection *target=nullptr;
  outcome=resolve_composed(device,request,model,target,input);
  if(outcome.ok) {
    if(target->json_binding)outcome=target->json_binding->invoke(target->json_binding->context,input,output);
    else if(request.command)outcome=target->json_command_handler(device,input,output);
    else if(target->json_result_provider)outcome=target->json_result_provider(device,output);
    else target->json_provider(device,output);
  }
  status.work_result_known=true;status.work_ok=outcome.ok;status.work_error_code=outcome.error.c_str();
}

class ComposedRequest final : public ServiceRequest {
public:
  ComposedRequest(DeviceApiContext &owner, const ComposedDeviceOperation &request)
      : device(owner), module(request.module), operation(request.operation),
        capability(request.capability), caller(request.caller_module), command(request.command) {}
  ~ComposedRequest() override {
#if CONFIG_LUNAREDU_SYSTEM_DIAGNOSTICS || CONFIG_LUNAREDU_LOCAL_DEBUG_SURFACE
    if(!started)record_diagnostic_trace(trace,DiagnosticPhase::DispatchFailed,qualified.c_str(),"composed_operation_not_started");
#endif
  }
  static bool clone(JsonVariantConst source,JsonDocument &target,size_t limit) {
    // Serialization followed by read-only parsing also owns strings originally
    // linked into the caller's JSON. A shallow variant copy is insufficient.
    const auto bytes = measureJson(source);
    if (bytes > limit) return false;
    std::string encoded;
    if (serializeJson(source, encoded) != bytes ||
        deserializeJson(target, encoded) != DeserializationError::Ok || target.overflowed()) return false;
    return true;
  }
  bool prepare(JsonVariantConst source,JsonObjectConst initial) {
    if(!clone(source,input,8192))return false;
    if(initial.size()) {
      if(!clone(initial,output,16384))return false;
    } else output.to<JsonObject>();
    return !output.overflowed();
  }
  void begin_trace() {
#if CONFIG_LUNAREDU_SYSTEM_DIAGNOSTICS || CONFIG_LUNAREDU_LOCAL_DEBUG_SURFACE
    if(diagnostic_capture_enabled()) {
      qualified=module+"."+operation;
      trace=begin_diagnostic_trace(qualified.c_str());
    }
#endif
  }
  void execute(ServiceOperationResult &status) override {
#if CONFIG_LUNAREDU_SYSTEM_DIAGNOSTICS || CONFIG_LUNAREDU_LOCAL_DEBUG_SURFACE
    started=true;record_diagnostic_trace(trace,DiagnosticPhase::Started,qualified.c_str());
#endif
    const ComposedDeviceOperation request{module.c_str(),operation.c_str(),capability.c_str(),command,caller.c_str()};
    // Composition/capability admission is checked after a foreign queue wait;
    // the target's existing handler still owns its domain authorization.
    execute_composed(device,request,input.as<JsonVariantConst>(),output.as<JsonObject>(),outcome,status);
    if(output.overflowed()||measureJson(output)>16384) {
      output.clear();outcome={false,507,"composed_operation_result_too_large",OperationCompletion::Unknown,OperationError::Internal};
    }
    status.work_result_known=true;status.work_ok=outcome.ok;status.work_error_code=outcome.error.c_str();
#if CONFIG_LUNAREDU_SYSTEM_DIAGNOSTICS || CONFIG_LUNAREDU_LOCAL_DEBUG_SURFACE
    record_diagnostic_trace(trace,outcome.ok?DiagnosticPhase::Returned:DiagnosticPhase::HandlerFailed,qualified.c_str(),outcome.error.c_str());
#endif
  }
  DeviceApiContext &device; // Composition-lived; pending calls fence retirement.
  std::string module,operation,capability,caller;
  bool command;
  JsonWorkspace allocator{0}; // Request-owned allocation, no idle result cache.
  JsonDocument input{&allocator},output{&allocator};
  DeviceOperationResult outcome;
#if CONFIG_LUNAREDU_SYSTEM_DIAGNOSTICS || CONFIG_LUNAREDU_LOCAL_DEBUG_SURFACE
  DiagnosticTrace trace;
  std::string qualified;
  bool started=false;
#endif
};
}

DeviceOperationResult check_composed_device_operation(DeviceApiContext &device,
    const ComposedDeviceOperation &request,JsonVariantConst input) {
  const DeviceOperationSpec *model=nullptr;const DeviceOperationProjection *projection=nullptr;
  return resolve_composed(device,request,model,projection,input);
}

DeviceOperationResult invoke_composed_device_operation(DeviceApiContext &device,
    const ComposedDeviceOperation &request,JsonVariantConst input,JsonObject output) {
  const DeviceOperationSpec *model=nullptr;const DeviceOperationProjection *projection=nullptr;
  auto result=resolve_composed(device,request,model,projection,input);
  if(!result.ok)return result;
  if(model->execution==DeviceOperationExecution::OwnerExecutor &&
      !device.runtime.current_task_owns_executor(model->owner_executor)) {
    auto owned=std::unique_ptr<ComposedRequest>(new(std::nothrow) ComposedRequest(device,request));
    if(!owned)return {false,503,"composed_operation_allocation_failed",OperationCompletion::NotStarted,OperationError::Unavailable};
    if(!owned->prepare(input,output))return {false,413,"composed_operation_input_unavailable",OperationCompletion::NotStarted,OperationError::InvalidArgument};
    owned->begin_trace();
    ServiceCall pending;
    std::string error;
    if(!device.runtime.submit_executor(model->owner_executor,std::move(owned),pending,error))
      return {false,503,error,OperationCompletion::NotStarted,OperationError::Unavailable};
    if(!device.runtime.wait_executor(pending,5000,error)) {
      const bool prevented=pending.cancel_before_start();
      // A running write may already have applied. Do not replay it, publish a
      // partial output, or retain the caller's stack/JSON after this return.
      if(prevented)return {false,504,"composed_operation_cancelled_before_start",OperationCompletion::NotStarted,OperationError::Cancelled};
      // Completion can be published between the wait deadline and cancellation
      // CAS. Consume that immutable result rather than reporting it unknown.
      if(pending.phase()!=ServiceCallPhase::Completed)
        return {false,504,"composed_operation_completion_unknown",OperationCompletion::Unknown,OperationError::DeadlineExceeded};
    }
    const auto *finished=static_cast<const ComposedRequest *>(pending.request());
    const auto *status=pending.result();
    if(!finished||!status||!status->ok)return {false,503,status?status->error:"composed_operation_result_unavailable",OperationCompletion::Unknown,OperationError::Internal};
    if(!output.set(finished->output.as<JsonObjectConst>()))return {false,507,"composed_operation_output_unavailable",OperationCompletion::Unknown,OperationError::Internal};
    return finished->outcome;
  }
  struct Call {DeviceApiContext &device;const ComposedDeviceOperation &request;
    JsonVariantConst input;JsonObject output;DeviceOperationResult result;};
  Call call{device,request,input,output,{}};
  const auto invoke=[](void *opaque,ServiceOperationResult &status) {
    auto &call=*static_cast<Call *>(opaque);
    execute_composed(call.device,call.request,call.input,call.output,call.result,status);
  };
  std::string error;
  if(!dispatch_device_operation(device.runtime,*model,invoke,&call,error,request.module))
    return {false,503,error.empty()?"operation_owner_unavailable":error,OperationCompletion::Unknown,OperationError::Unavailable};
  return call.result;
}
}
