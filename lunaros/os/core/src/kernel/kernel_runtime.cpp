#include "kernel/service_maintenance.h"
#include "kernel/runtime_internal.h"
#include "kernel/runtime.h"
#include "kernel/platform_operation.h"

#include "kernel/services/service_host_internal.h"
#include "kernel/services/service_registry_internal.h"
#include "kernel/execution/service_executor_internal.h"
#include "kernel/tasks/task_registry_internal.h"
#include <cstring>
#ifdef ESP_PLATFORM
#include "kernel/memory_status.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

namespace lunaredu::lunaros {

struct KernelRuntime::Impl {
  ServiceRegistry registry;
  ServiceExecutorRegistry executors;
  ManagedTaskRegistry tasks;
  ServiceHost host;
  ServiceMaintenanceCoordinator maintenance;
  bool enforce_task_owners = false;

  Impl()
      : registry(), executors(), tasks(),
        host(registry, &executors) {}
};

KernelRuntime::KernelRuntime() : impl_(new Impl()) {}

KernelRuntime::~KernelRuntime() {
  (void)unbind_platform_execution(impl_->tasks.execution(),this);
  delete impl_;
}

bool KernelRuntime::can_reset_composition(std::string &error) const {
  if (!impl_->tasks.execution().can_reset()) { error="execution_reservations_active"; return false; }
  return impl_->registry.can_reset(error) && impl_->tasks.can_reset(error);
}

bool KernelRuntime::initialize(std::string &error) {
#ifdef ESP_PLATFORM
  if (!bind_platform_execution(impl_->tasks.execution(),this)) {
    error="platform_execution_already_bound";return false;
  }
  system::initialize_memory_pressure_monitor();
#endif
  return impl_->registry.initialize(error);
}
bool KernelRuntime::declare_task_owner(ManagedTaskOwnerKind kind, const char *id, std::string &error) {
  if (kind != ManagedTaskOwnerKind::Service && kind != ManagedTaskOwnerKind::Application &&
      kind != ManagedTaskOwnerKind::Subsystem) { error = "managed_task_invalid_owner_kind"; return false; }
  if (kind == ManagedTaskOwnerKind::Service) {
    if (!id || !impl_->registry.service_registered(id)) { error = "managed_task_service_owner_not_found"; return false; }
    error.clear(); return true;
  }
  return impl_->tasks.declare_owner(kind, id, error);
}
bool KernelRuntime::forget_task_owner(ManagedTaskOwnerKind kind, const char *id) {
  return impl_->tasks.forget_owner(kind, id);
}
bool KernelRuntime::validate_task_owner(const ManagedTaskDefinition &definition, std::string &error) const {
  const char *id = definition.owner_service_id;
  const bool registered = definition.owner_kind == ManagedTaskOwnerKind::Service
      ? id && impl_->registry.service_registered(id)
      : impl_->tasks.owner_registered(definition.owner_kind, id);
  if (!registered) { error = "managed_task_owner_not_registered"; return false; }
  error.clear(); return true;
}
bool KernelRuntime::enforce_task_ownership(std::string &error) {
  if (!impl_->tasks.can_reset(error)) return false;
  impl_->enforce_task_owners = true; error.clear(); return true;
}
bool KernelRuntime::task_ownership_enforced() const { return impl_->enforce_task_owners; }

ServiceRegistry &KernelRuntime::service_registry() { return impl_->registry; }

ServiceHost &KernelRuntime::service_host() { return impl_->host; }

ManagedTaskRegistry &KernelRuntime::task_registry() { return impl_->tasks; }
ExecutionAdmission &KernelRuntime::execution_admission() { return impl_->tasks.execution(); }
const ExecutionAdmission &KernelRuntime::execution_admission() const { return impl_->tasks.execution(); }
ExecutionLease KernelRuntime::task_execution_lease(const char *id) const { return impl_->tasks.execution_lease(id); }
ExecutionContract KernelRuntime::current_task_execution(ExecutionContract declared) const {
#ifdef ESP_PLATFORM
  declared.priority=uxTaskPriorityGet(nullptr);
  declared.core=xPortGetCoreID();
#endif
  return declared;
}

ServiceRegistrySnapshot KernelRuntime::service_snapshot() const {
  auto snapshot=impl_->registry.snapshot();impl_->host.describe_uses(snapshot);return snapshot;
}

bool KernelRuntime::reset(std::string &error) {
  // Composition is owner-serialized. Check every domain before clearing any
  // state so a live service/executor cannot leave a partially reset runtime.
  if (!can_reset_composition(error) ||
      !impl_->executors.can_reset(&error)) {
    return false;
  }
  if (!impl_->tasks.reset(error)) {
    return false;
  }
  if (!impl_->executors.reset(&error)) {
    return false;
  }
  impl_->host.reset();
  impl_->registry.reset();
  error.clear();
  return true;
}

bool KernelRuntime::start_all_services(std::string &error) {
  return impl_->host.start_all(error);
}
bool KernelRuntime::reconcile_services(std::string &error) {
  return impl_->host.reconcile(error);
}
bool KernelRuntime::reserve_service_resource(const char *resource, const char *owner,
    ServiceResourceWindow &window, std::string &error) {
  return impl_->host.reserve_resource(resource, owner, window, error);
}
bool KernelRuntime::release_service_resource(ServiceResourceWindow &window, std::string &error) {
  return impl_->host.release_resource(window, error);
}
bool KernelRuntime::request_service_use(const char *id, const char *owner,
    ServiceUse &use, std::string &error) {
  return impl_->host.request_use(id,owner,use,error);
}
bool KernelRuntime::release_service_use(const char *owner, ServiceUse &use, std::string &error) {
  return impl_->host.release_use(owner,use,error);
}

bool KernelRuntime::stop_all_services(std::string &error) {
  return impl_->host.stop_all(error);
}

bool KernelRuntime::restart_service(const char *id, std::string &error) {
  if (id == nullptr || id[0] == '\0') {
    error = "invalid_service_id";
    return false;
  }
  ServiceState state = ServiceState::Registered;
  if (!impl_->registry.state(id, state)) {
    error = "service_not_found";
    return false;
  }
  // Do not interrupt a foreground application when the host will refuse to
  // stop this provider because another running service still requires it.
  if (state != ServiceState::Registered && state != ServiceState::Stopped) {
    const auto snapshot = impl_->registry.snapshot();
    for (size_t index = 0; index < snapshot.service_count; ++index) {
      const auto &candidate = snapshot.services[index];
      if (candidate.state != ServiceState::Ready &&
          candidate.state != ServiceState::Degraded &&
          candidate.state != ServiceState::Starting &&
          candidate.state != ServiceState::Stopping &&
          candidate.state != ServiceState::Failed) continue;
      for (size_t dependency = 0; dependency < candidate.dependency_count;
           ++dependency) {
        if (candidate.dependencies[dependency].required &&
            candidate.dependencies[dependency].id != nullptr &&
            std::strcmp(candidate.dependencies[dependency].id, id) == 0) {
          error = "service_has_active_dependent:" + std::string(candidate.id);
          return false;
        }
      }
    }
  }
  const auto coordinator = impl_->maintenance;
  if (coordinator.before_restart != nullptr &&
      !coordinator.before_restart(coordinator.context, id, error)) return false;
  const bool restarted = impl_->host.restart(id, error);
  if (coordinator.after_restart != nullptr &&
      !coordinator.after_restart(coordinator.context, id, restarted, error))
    return false;
  return restarted;
}

bool KernelRuntime::can_bind_service_maintenance(std::string &error) const {
  if (impl_->maintenance.context != nullptr) {
    error = "service_maintenance_coordinator_already_bound";
    return false;
  }
  error.clear(); return true;
}

bool KernelRuntime::bind_service_maintenance(
    ServiceMaintenanceCoordinator coordinator, std::string &error) {
  if (coordinator.context == nullptr || coordinator.before_restart == nullptr ||
      coordinator.after_restart == nullptr) {
    error = "invalid_service_maintenance_coordinator";
    return false;
  }
  if (!can_bind_service_maintenance(error)) return false;
  impl_->maintenance = coordinator;
  error.clear();
  return true;
}

void KernelRuntime::unbind_service_maintenance(void *context) {
  if (impl_->maintenance.context == context) impl_->maintenance = {};
}


bool KernelRuntime::report_service_ready(const char *id, const char *detail,
                                         std::string &error) {
  return id != nullptr && impl_->host.report_ready(id, detail, error);
}


bool KernelRuntime::report_service_failed(const char *id, const char *detail,
                                          std::string &error) {
  return id != nullptr && impl_->host.report_failed(id, detail, error);
}

bool KernelRuntime::service_registered(const char *id) const {
  return id != nullptr && impl_->registry.service_registered(id);
}

bool KernelRuntime::service_available(const char *id) const {
  return id != nullptr && impl_->registry.service_available(id);
}

const char *KernelRuntime::service_owner_executor(const char *id) const {
  return impl_->registry.owner_executor(id);
}

bool KernelRuntime::required_services_ready(std::string *reason) const {
  return impl_->registry.required_services_ready(reason);
}
bool KernelRuntime::service_state(const char *id, ServiceState &state) const {
  return impl_->registry.state(id, state);
}

uint32_t KernelRuntime::service_revision() const { return impl_->registry.revision(); }

bool KernelRuntime::setup_executors(
    std::initializer_list<ServiceExecutorDefinition> definitions,
    std::string *error) {
  return impl_->executors.setup(definitions, error);
}
bool KernelRuntime::setup_executors(const ServiceExecutorDefinition *definitions, size_t count,
    std::string *error) {
  return impl_->executors.setup_range(definitions,count,error);
}

bool KernelRuntime::bind_executor(const char *id, std::string *error) {
  return impl_->executors.bind(id, error);
}

bool KernelRuntime::prepare_executor_retirement(const char *id, std::string *error) {
  return impl_->executors.prepare_retirement(id, error);
}

bool KernelRuntime::unbind_executor(const char *id, std::string *error) {
  return impl_->executors.unbind(id, error);
}

bool KernelRuntime::pump_executor(const char *id, uint32_t wait_timeout_ms,
                                  std::string *error) {
  return impl_->executors.pump(id, wait_timeout_ms, error);
}

size_t KernelRuntime::executor_snapshot(ServiceExecutorSnapshot *output,
                                        size_t capacity) const {
  return impl_->executors.snapshot(output, capacity);
}

uint32_t KernelRuntime::executor_pending_calls(const char *id) const {
  return impl_->executors.pending_calls(id);
}

bool KernelRuntime::current_task_owns_executor() const {
  return impl_->executors.current_task_owns();
}
bool KernelRuntime::current_task_owns_executor(const char *id) const {
  return impl_->executors.current_task_owns(id);
}

bool KernelRuntime::invoke_executor(const char *id, ServiceOperation operation,
                                    void *context, std::string &error,
                                    uint32_t admission_timeout_ms) {
  return impl_->executors.invoke(id, operation, context, error,
                                 admission_timeout_ms);
}

bool KernelRuntime::submit_executor(const char *id, std::unique_ptr<ServiceRequest> request,
    ServiceCall &call, std::string &error, uint32_t timeout) {
  return impl_->executors.submit(id, std::move(request), call, error, timeout);
}
bool KernelRuntime::wait_executor(ServiceCall &call, uint32_t timeout, std::string &error) {
  return impl_->executors.wait(call, timeout, error);
}
bool KernelRuntime::submit_executor_deferred(const char *id,std::unique_ptr<ServiceRequest> request,
    ServiceCall &call,std::string &error) {
  return impl_->executors.submit(id,std::move(request),call,error,0,true);
}
ExecutorReadiness KernelRuntime::request_executor_activation(const char *id) {
  return impl_->executors.request_activation(id);
}
bool KernelRuntime::try_submit_executor_deferred(const char *id,std::unique_ptr<ServiceRequest> &request,
    ServiceCall &call,std::string &error) {
  return impl_->executors.submit_retained(id,request,call,error,0,true);
}

bool KernelRuntime::prepare_task_admission(std::string &error) {
  return impl_->tasks.ensure_configured(error);
}

bool KernelRuntime::task_active(const char *id) const {
  return impl_->tasks.contains(id);
}

bool KernelRuntime::task_running(const char *id) const {
  return impl_->tasks.running(id);
}

bool KernelRuntime::task_active(const char *id, ManagedTaskOwnerKind kind,
                               const char *owner_id) const {
  return impl_->tasks.contains(id, kind, owner_id);
}

bool KernelRuntime::task_running(const char *id, ManagedTaskOwnerKind kind,
                                const char *owner_id) const {
  return impl_->tasks.running(id, kind, owner_id);
}

ManagedTaskRegistrySnapshot KernelRuntime::task_snapshot() const {
  return impl_->tasks.snapshot();
}

ManagedTaskSummary KernelRuntime::task_summary() const {
  return impl_->tasks.summary();
}

ManagedTaskMetadataSnapshot KernelRuntime::task_metadata() const {
  return impl_->tasks.metadata();
}

bool setup_core_runtime(KernelRuntime &runtime,
                        std::initializer_list<ServiceExecutorDefinition> executors,
                        std::string &error) {
  return setup_core_runtime(runtime,executors.begin(),executors.size(),error);
}
bool setup_core_runtime(KernelRuntime &runtime,const ServiceExecutorDefinition *executors,
    size_t count,std::string &error) {
  error.clear();
  // Clear only a quiescent runtime before publishing the next composition.
  // Resetting after setup would erase the freshly declared executors/tasks.
  if (!runtime.reset(error)) {
    return false;
  }
  if (!runtime.initialize(error)) {
    return false;
  }
  if (!runtime.prepare_task_admission(error)) {
    return false;
  }
  if (!runtime.setup_executors(executors,count,&error)) {
    return false;
  }
  return true;
}

} // namespace lunaredu::lunaros
