#include "app/lifecycle/app_capability.h"

#include <array>
#include <cstring>
#include <mutex>
#include <utility>
#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif
#if CONFIG_LUNAREDU_SYSTEM_DIAGNOSTICS || CONFIG_LUNAREDU_LOCAL_DEBUG_SURFACE
#include "diagnostics/trace.h"
#include <cstdio>
#endif

namespace lunaredu::app {
namespace {
bool text_valid(const char *text) { return text != nullptr && text[0] != '\0'; }
bool provider_allows(const CapabilityDefinition &definition, const char *caller) {
  if (!text_valid(caller)) return false;
  for (size_t i = 0; i < definition.allowed_caller_count; ++i) {
    if (std::strcmp(caller, definition.allowed_callers[i]) == 0) return true;
  }
  return false;
}
bool fail(std::string &error, const char *message) { error = message; return false; }
bool provider_available(CapabilityRegistry &registry,const CapabilityDefinition &definition,std::string &reason) {
  return definition.availability_detail?definition.availability_detail(registry,definition.context,reason):
    !definition.available||definition.available(registry,definition.context);
}
} // namespace

bool CapabilityRegistry::allowed(const CapabilityDefinition &definition, const char *caller) {
  if (!provider_allows(definition, caller)) return false;
  CapabilityAccessPolicy policy;
  void *context;
  {
    std::lock_guard<std::mutex> lock(mutex);
    policy = access_policy;
    context = access_context;
  }
  return !policy || policy(caller, definition, context);
}
bool CapabilityRegistry::lookup(const char *id, CapabilityDefinition &out) {
  if (!text_valid(id)) return false;
  std::lock_guard<std::mutex> lock(mutex);
  for (size_t i = 0; i < count; ++i) {
    if (std::strcmp(id, definitions[i].id) == 0) {
      out = definitions[i];
      return true;
    }
  }
  return false;
}

bool CapabilityRegistry::configure_capability_execution(CapabilityExecutionPort port,std::string &error) {
  if(!port.owns||!port.invoke)return fail(error,"invalid_capability_execution_port");
  std::lock_guard<std::mutex> lock(mutex);
  if(execution_port.owns==port.owns&&execution_port.invoke==port.invoke&&execution_port.context==port.context){error.clear();return true;}
  if(sealed||execution_port.owns)return fail(error,"capability_execution_port_already_bound");
  execution_port=port;error.clear();return true;
}


bool CapabilityRegistry::declare_atomically(bool (*declare)(CapabilityRegistry &, std::string &),
                                     std::string &error) {
  if (!declare) return true;
  size_t original_count;
  size_t original_consumer_count;
  {
    std::lock_guard<std::mutex> lock(mutex);
    original_count = count;
    original_consumer_count = consumer_count;
  }
  // Do not hold the registry mutex across application code. Registration is
  // append-only and product composition has a single owner before startup.
  if (declare(*this, error)) return true;
  std::lock_guard<std::mutex> lock(mutex);
  while (count > original_count) definitions[--count] = {};
  while (consumer_count > original_consumer_count) consumers[--consumer_count] = {};
  return false;
}

bool CapabilityRegistry::register_capability(const CapabilityDefinition &definition, std::string &error) {
  if(definition.executor!=lunaros::OperationExecutor::Main&&definition.executor!=lunaros::OperationExecutor::NetworkIo)
    return fail(error,"invalid_application_capability_executor");
  if (!text_valid(definition.id) || !text_valid(definition.provider_app) ||
      definition.contract == 0 || definition.request_size == 0 ||
      definition.response_size == 0 || definition.invoke == nullptr ||
      definition.allowed_callers == nullptr || definition.allowed_caller_count == 0) {
    return fail(error, "invalid_application_capability");
  }
  for (size_t i = 0; i < definition.allowed_caller_count; ++i) {
    if (!text_valid(definition.allowed_callers[i])) return fail(error, "invalid_capability_caller");
    for (size_t j = 0; j < i; ++j)
      if (std::strcmp(definition.allowed_callers[i], definition.allowed_callers[j]) == 0)
        return fail(error, "duplicate_capability_caller");
  }
  std::lock_guard<std::mutex> lock(mutex);
  for (size_t i = 0; i < count; ++i) {
    const auto &existing = definitions[i];
    if (std::strcmp(existing.id, definition.id) != 0) continue;
    bool same = std::strcmp(existing.provider_app, definition.provider_app) == 0 &&
        existing.contract == definition.contract && existing.request_size == definition.request_size &&
        existing.response_size == definition.response_size && existing.invoke == definition.invoke &&
        existing.available == definition.available && existing.context == definition.context &&
        existing.allowed_caller_count == definition.allowed_caller_count&&existing.executor==definition.executor&&
        existing.availability_detail==definition.availability_detail;
    for (size_t j = 0; same && j < existing.allowed_caller_count; ++j)
      same = std::strcmp(existing.allowed_callers[j], definition.allowed_callers[j]) == 0;
    if (!same) return fail(error, "application_capability_conflict");
    error.clear();
    return true;
  }
  if (sealed) return fail(error, "application_capability_registry_sealed");
  if (count == definitions.size()) return fail(error, "application_capability_registry_full");
  definitions[count++] = definition;
  error.clear();
  return true;
}

bool CapabilityRegistry::configure_capability_access(CapabilityAccessPolicy policy, void *context,
                                 std::string &error) {
  std::lock_guard<std::mutex> lock(mutex);
  if (sealed) return fail(error, "application_capability_registry_sealed");
  if (!policy && context) return fail(error, "invalid_capability_access_policy");
  access_policy = policy;
  access_context = context;
  error.clear();
  return true;
}

bool CapabilityRegistry::register_capability_consumer(const CapabilityConsumerDefinition &consumer,
                                  std::string &error) {
  if (!text_valid(consumer.app_id) ||
      consumer.dependency_count > kCapabilityDependencyCapacity ||
      (consumer.dependency_count && !consumer.dependencies))
    return fail(error, "invalid_capability_consumer");
  for (size_t i = 0; i < consumer.dependency_count; ++i) {
    const auto &dependency = consumer.dependencies[i];
    if (!text_valid(dependency.id) || !dependency.contract)
      return fail(error, "invalid_capability_dependency");
    for (size_t j = 0; j < i; ++j)
      if (std::strcmp(dependency.id, consumer.dependencies[j].id) == 0)
        return fail(error, "duplicate_capability_dependency");
  }
  std::lock_guard<std::mutex> lock(mutex);
  for (size_t i = 0; i < consumer_count; ++i) {
    const auto &existing = consumers[i];
    if (std::strcmp(existing.app_id, consumer.app_id) != 0) continue;
    bool same = existing.dependency_count == consumer.dependency_count;
    for (size_t j = 0; same && j < existing.dependency_count; ++j) {
      const auto &a = existing.dependencies[j];
      const auto &b = consumer.dependencies[j];
      same = std::strcmp(a.id, b.id) == 0 && a.contract == b.contract && a.required == b.required;
    }
    if (!same) return fail(error, "capability_consumer_conflict");
    error.clear();
    return true;
  }
  if (sealed) return fail(error, "application_capability_registry_sealed");
  if (consumer_count == consumers.size()) return fail(error, "capability_consumer_registry_full");
  consumers[consumer_count++] = consumer;
  error.clear();
  return true;
}

bool CapabilityRegistry::seal_capabilities(std::string &error) {
  // This is a boot-only transaction: composition is serialized by the product.
  // Application policy callbacks are deliberately evaluated without our mutex.
  size_t limit;
  {
    std::lock_guard<std::mutex> lock(mutex);
    if (sealed) { error.clear(); return true; }
    limit = consumer_count;
  }
  for (size_t i = 0; i < limit; ++i) {
    CapabilityConsumerDefinition consumer;
    { std::lock_guard<std::mutex> lock(mutex); consumer = consumers[i]; }
    for (size_t j = 0; j < consumer.dependency_count; ++j) {
      const auto &dependency = consumer.dependencies[j];
      CapabilityDefinition provider;
      const bool found = lookup(dependency.id, provider);
      const char *reason = !found ? "missing" :
          provider.contract != dependency.contract ? "contract_mismatch" :
          !allowed(provider, consumer.app_id) ? "denied" : nullptr;
      if (reason && (dependency.required || (found && provider.contract != dependency.contract))) {
        error = std::string("capability_dependency_") + reason + ":" + consumer.app_id + ":" + dependency.id;
        return false;
      }
    }
  }
  std::lock_guard<std::mutex> lock(mutex);
  sealed = true;
  error.clear();
  return true;
}

bool CapabilityRegistry::capabilities_sealed() {
  std::lock_guard<std::mutex> lock(mutex);
  return sealed;
}

void CapabilityRegistry::reset_capabilities() {
  std::lock_guard<std::mutex> lock(mutex);
  definitions.fill({});
  consumers.fill({});
  count = consumer_count = 0;
  access_policy = nullptr;
  access_context = nullptr;
  execution_port={};
  sealed = false;
}

bool CapabilityRegistry::invoke_capability(const char *id, const CapabilityCall &call, std::string &error) {
  using lunaredu::lunaros::OperationCompletion;
  using lunaredu::lunaros::OperationError;
  if(call.outcome)*call.outcome={OperationCompletion::NotStarted,OperationError::None};
  CapabilityDefinition definition;
  const bool found = lookup(id, definition);
#if CONFIG_LUNAREDU_SYSTEM_DIAGNOSTICS || CONFIG_LUNAREDU_LOCAL_DEBUG_SURFACE
  using namespace lunaredu::lunaros;
  char qualified[160]{};
  DiagnosticTrace trace;
  if (diagnostic_capture_enabled()) {
    std::snprintf(qualified, sizeof(qualified), "native.%s", found ? definition.id : "capability");
    const bool parent_current = diagnostic_trace_current({call.diagnostic_parent_trace_id,
        call.diagnostic_generation, call.diagnostic_request_sequence});
    trace = begin_diagnostic_trace(qualified, parent_current ? call.diagnostic_request_sequence : 0,
        parent_current ? call.diagnostic_parent_trace_id : 0);
  }
#endif
  const auto rejected = [&](const char *code,OperationError category) {
    if(call.outcome)*call.outcome={OperationCompletion::NotStarted,category};
#if CONFIG_LUNAREDU_SYSTEM_DIAGNOSTICS || CONFIG_LUNAREDU_LOCAL_DEBUG_SURFACE
    record_diagnostic_trace(trace, DiagnosticPhase::DispatchFailed, qualified, code);
#endif
    return fail(error, code);
  };
  if (!found) return rejected("application_capability_not_found",OperationError::NotFound);
  const auto execution=lunaros::capture_operation_context(call.execution);
  if(execution.ancestry_exhausted)return rejected("application_capability_context_capacity",OperationError::Unavailable);
  if(execution.cancelled())return rejected("application_capability_cancelled",OperationError::Cancelled);
  if(execution.expired())return rejected("application_capability_deadline_exceeded",OperationError::DeadlineExceeded);
  lunaros::OperationContextScope operation_scope(execution);
  if (!allowed(definition, call.caller)) return rejected("application_capability_denied",OperationError::Forbidden);
  if (call.contract != definition.contract || call.request_size != definition.request_size ||
      call.response_size != definition.response_size || !call.request || !call.response)
    return rejected("application_capability_contract_mismatch",OperationError::InvalidArgument);
  CapabilityExecutionPort port;
  {std::lock_guard<std::mutex> lock(mutex);port=execution_port;}
  if(port.owns&&!port.owns(port.context,definition.executor)) {
    struct Dispatch {
      CapabilityRegistry &registry;const char *id;const CapabilityCall &call;
      std::string &error;bool ran=false,ok=false;
    } dispatch{*this,id,call,error};
    std::string admission_error;
    const bool accepted=port.invoke(port.context,definition.executor,[](void *opaque){
      auto &work=*static_cast<Dispatch *>(opaque);work.ran=true;
      work.ok=work.registry.invoke_capability(work.id,work.call,work.error);
    },&dispatch,execution.remaining(100),admission_error);
    if(dispatch.ran)return dispatch.ok;
    (void)accepted;
    if(execution.cancelled())return rejected("application_capability_cancelled",OperationError::Cancelled);
    if(execution.expired())return rejected("application_capability_deadline_exceeded",OperationError::DeadlineExceeded);
    return rejected(admission_error.empty()?"capability_owner_not_admitted":admission_error.c_str(),OperationError::Unavailable);
  }
  // Never hold the registry lock across application code or nested calls.
  const bool available=evaluate_availability(definition);
  if(execution.cancelled())return rejected("application_capability_cancelled",OperationError::Cancelled);
  if(execution.expired())return rejected("application_capability_deadline_exceeded",OperationError::DeadlineExceeded);
  if(!available)return rejected("application_capability_unavailable",OperationError::Unavailable);
  error.clear();
  auto dispatched = call;
  dispatched.execution=execution;
  dispatched.catalog = this;
#if CONFIG_LUNAREDU_SYSTEM_DIAGNOSTICS || CONFIG_LUNAREDU_LOCAL_DEBUG_SURFACE
  dispatched.diagnostic_parent_trace_id = trace.id;
  dispatched.diagnostic_generation = trace.generation;
  dispatched.diagnostic_request_sequence = trace.request_sequence;
  record_diagnostic_trace(trace, DiagnosticPhase::Started, qualified);
#endif
  if(call.outcome)*call.outcome={OperationCompletion::Completed,OperationError::None};
  const bool ok = definition.invoke(definition.context, dispatched, error);
  if(!ok&&call.outcome&&call.outcome->category==OperationError::None) {
    if(call.outcome->completion==OperationCompletion::Completed)call.outcome->completion=OperationCompletion::Unknown;
    call.outcome->category=OperationError::Internal;
  }
#if CONFIG_LUNAREDU_SYSTEM_DIAGNOSTICS || CONFIG_LUNAREDU_LOCAL_DEBUG_SURFACE
  record_diagnostic_trace(trace, ok ? DiagnosticPhase::Returned : DiagnosticPhase::HandlerFailed, qualified,
      ok ? nullptr : "native_provider_failed");
#endif
  return ok;
}

bool CapabilityRegistry::capability_available(const char *id, const char *caller,std::string *reason) {
  if(reason)reason->clear();
  CapabilityDefinition definition;
  return lookup(id, definition) && allowed(definition, caller) &&
      evaluate_availability(definition,reason);
}

bool CapabilityRegistry::evaluate_availability(const CapabilityDefinition &definition,std::string *reason) {
  if(!definition.available&&!definition.availability_detail)return true;
  CapabilityExecutionPort port;
  {std::lock_guard<std::mutex> lock(mutex);port=execution_port;}
  const auto budget=lunaros::capture_operation_context();
  if(budget.cancelled()||budget.expired()){
    if(reason)*reason=budget.expired()?"application_capability_deadline_exceeded":"application_capability_cancelled";
    return false;
  }
  std::string detail;
  if(!port.owns||port.owns(port.context,definition.executor)) {
    const bool value=provider_available(*this,definition,detail);if(reason)*reason=std::move(detail);return value;
  }
  struct Probe {CapabilityRegistry &registry;const CapabilityDefinition &definition;std::string &reason;
    bool value=false,ran=false;} probe{*this,definition,detail};
  std::string error;
  (void)port.invoke(port.context,definition.executor,[](void *opaque){
    auto &work=*static_cast<Probe *>(opaque);work.ran=true;
    if(!lunaros::operation_cancellation_requested()&&lunaros::operation_remaining_timeout_ms(100))
      work.value=provider_available(work.registry,work.definition,work.reason);
  },&probe,budget.remaining(100),error);
  if(reason)*reason=probe.ran?std::move(detail):std::move(error);
  return probe.ran&&probe.value;
}

bool CapabilityRegistry::find_capability(const char *id, const char *caller, CapabilityDefinition &out) {
  CapabilityDefinition definition;
  if (!lookup(id, definition) || !allowed(definition, caller)) return false;
  out = definition;
  return true;
}

void CapabilityRegistry::visit_capabilities(const char *caller, CapabilityVisitor visitor, void *context, uint32_t contract_filter,
                        bool evaluate_availability) {
  if (!visitor) return;
  size_t limit;
  { std::lock_guard<std::mutex> lock(mutex); limit = count; }
  for (size_t i = 0; i < limit; ++i) {
    CapabilityDefinition definition;
    { std::lock_guard<std::mutex> lock(mutex); definition = definitions[i]; }
    if ((!contract_filter || definition.contract == contract_filter) && allowed(definition, caller))
      visitor(definition, evaluate_availability &&
          this->evaluate_availability(definition), context);
  }
}
} // namespace lunaredu::app
