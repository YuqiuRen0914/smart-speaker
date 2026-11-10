#pragma once

#include <cstdint>
#include <initializer_list>
#include <string>
#include "kernel/service_executor.h"
#include "kernel/service.h"
#include "kernel/managed_task.h"

namespace lunaredu::lunaros {

struct ProductDefinition;
class ServiceRegistry;
class ServiceHost;
class ServiceExecutorRegistry;
class ManagedTaskRegistry;

struct ServiceMaintenanceCoordinator;

// Owns the complete kernel execution context: service registry/lifecycle,
// executor bindings, and managed task admission. Serialize composition,
// start/stop and reset on the owner task; stop and reset before destroying
// callback contexts or this runtime.
// Snapshots borrow metadata until reset/destruction.
class KernelRuntime {
public:
  KernelRuntime();
  ~KernelRuntime();

  KernelRuntime(const KernelRuntime &) = delete;
  KernelRuntime &operator=(const KernelRuntime &) = delete;

  bool initialize(std::string &error);
  bool install_product(const ProductDefinition &definition,
                       std::string &error);
  bool start_all_services(std::string &error);
  bool stop_all_services(std::string &error);
  // Owner-serialized, bounded reconciliation. Pending shutdown retains
  // ownership; declarations and API metadata remain installed.
  bool reconcile_services(std::string &error);
  // A pending reservation remains owned by window. Poll after reconciliation;
  // no conflicting operation may allocate until this returns true. Release
  // only after that operation has actually retired, including failed starts.
  bool reserve_service_resource(const char *resource, const char *owner,
      ServiceResourceWindow &window, std::string &error);
  bool release_service_resource(ServiceResourceWindow &window, std::string &error);
  // Thread-safe native demand publication; never invokes another executor.
  // Startup/retirement happens in normal owner reconciliation.
  bool request_service_use(const char *id, const char *owner, ServiceUse &, std::string &error);
  bool release_service_use(const char *owner, ServiceUse &, std::string &error);
  // Explicit maintenance recovery. The service must have no active required
  // dependents; startup also respects declared demand and resource windows.
  bool restart_service(const char *id, std::string &error);
  bool reset(std::string &error);
  // Owner-serialized read-only admission: services are Registered/Stopped and
  // all managed tasks are retired. Bound composition executors are preserved.
  bool can_reset_composition(std::string &error) const;

  bool report_service_ready(const char *id, const char *detail, std::string &error);

  bool report_service_failed(const char *id, const char *detail, std::string &error);
  bool service_available(const char *id) const;
  // Declaration presence is distinct from Running availability.
  bool service_registered(const char *id) const;
  // Allocation-free own lifecycle state, including retirement in progress.
  bool service_state(const char *id, ServiceState &state) const;
  // Immutable metadata, allocation-free. Borrowed until composition reset or
  // destruction, like service_snapshot's id/title/executor references.
  const char *service_owner_executor(const char *id) const;
  bool required_services_ready(std::string *reason = nullptr) const;
  uint32_t service_revision() const;
  ServiceRegistrySnapshot service_snapshot() const;
  bool setup_executors(std::initializer_list<ServiceExecutorDefinition> definitions,
                       std::string *error = nullptr);
  bool setup_executors(const ServiceExecutorDefinition *definitions, size_t count,
                       std::string *error = nullptr);
  bool bind_executor(const char *id, std::string *error = nullptr);
  bool unbind_executor(const char *id, std::string *error = nullptr);
  bool pump_executor(const char *id, uint32_t wait_timeout_ms,
                     std::string *error = nullptr);
  size_t executor_snapshot(ServiceExecutorSnapshot *output,
                           size_t capacity) const;
  // In-flight callers, including bounded activation waits and accepted work.
  // No task access, stack capture or snapshot allocation. A retiring owner
  // must still use unbind_executor's atomic admission/quiescence check.
  uint32_t executor_pending_calls(const char *id) const;
  bool current_task_owns_executor() const;
  bool current_task_owns_executor(const char *id) const;
  // Optional demand activation, admission gate and queue share this deadline.
  // Once accepted, the caller waits for completion; timeout never cancels or
  // replays a callback/side effect whose context belongs to that caller.
  bool invoke_executor(const char *id, ServiceOperation operation, void *context,
                       std::string &error,
                       uint32_t admission_timeout_ms = 100);
  // Request and result outlive the caller. Same-owner submission may execute
  // inline. Foreign admission is bounded; accepted queue/running work retains
  // executor demand until physically complete, even if the ticket is dropped.
  bool submit_executor(const char *id, std::unique_ptr<ServiceRequest>,
                       ServiceCall &, std::string &error,
                       uint32_t admission_timeout_ms = 100);
  // Nonwaiting admission; a same-owner callback runs on a later pump.
  // Explicit single-owner host seams without a queue reject this operation.
  bool submit_executor_deferred(const char *id,std::unique_ptr<ServiceRequest>,
      ServiceCall &,std::string &error);
  // Signals a dormant owner without waiting. Retirement remains closed; a
  // subsequent lifetime may bind before the caller retries admission.
  ExecutorReadiness request_executor_activation(const char *id);
  // Keeps caller ownership on rejected admission. No callback runs inline.
  bool try_submit_executor_deferred(const char *id,std::unique_ptr<ServiceRequest> &,
      ServiceCall &,std::string &error);
  // Deadline limits observation only. False may mean accepted work is still
  // running; never automatically replay a write after completion_timeout.
  bool wait_executor(ServiceCall &, uint32_t completion_timeout_ms,
                     std::string &error);
  bool prepare_task_admission(std::string &error);
  // Service owners must exist in the service graph. Applications are declared
  // by AppController; resident product/OS subsystems declare their own identity.
  bool declare_task_owner(ManagedTaskOwnerKind, const char *id, std::string &error);
  bool forget_task_owner(ManagedTaskOwnerKind, const char *id);
  bool validate_task_owner(const ManagedTaskDefinition &, std::string &error) const;
  bool enforce_task_ownership(std::string &error);
  bool task_ownership_enforced() const;
  ManagedTaskRegistrySnapshot task_snapshot() const;
  // Count and declared stack accounting; does not inspect native task stacks.
  ManagedTaskSummary task_summary() const;
  // Owned task declarations, without native stack measurements.
  ManagedTaskMetadataSnapshot task_metadata() const;
  // Shared time/effect admission for tasks, foreground work and operations.
  ExecutionAdmission &execution_admission();
  const ExecutionAdmission &execution_admission() const;
  // Trusted owner may use its task's existing reservation instead of counting
  // the same work twice. The task wrapper remains responsible for retirement.
  ExecutionLease task_execution_lease(const char *id) const;
  ExecutionContract current_task_execution(ExecutionContract declared) const;
  // Active includes admitted tasks until wrapper retirement releases the entry;
  // running additionally requires a published native handle. Presence queries
  // do not capture stack watermarks or allocate diagnostic snapshots.
  bool task_active(const char *id) const;
  bool task_running(const char *id) const;
  bool task_active(const char *id, ManagedTaskOwnerKind owner_kind,
                   const char *owner_id) const;
  bool task_running(const char *id, ManagedTaskOwnerKind owner_kind,
                    const char *owner_id) const;

private:
  friend class KernelRuntimeAccess;
  friend class ServiceMaintenanceAccess;
  friend class ExecutorTaskGroup;
  // Only the task-group owner closes admission and drains before leaving.
  bool prepare_executor_retirement(const char *id, std::string *error);
  bool can_bind_service_maintenance(std::string &error) const;
  bool bind_service_maintenance(ServiceMaintenanceCoordinator coordinator,
                                std::string &error);
  void unbind_service_maintenance(void *context);
  ServiceRegistry &service_registry();
  ServiceHost &service_host();
  ManagedTaskRegistry &task_registry();

  struct Impl;
  Impl *impl_ = nullptr;

  friend bool install_product_definition(KernelRuntime &,
                                         const ProductDefinition &,
                                         std::string &);
};

// Initializes this runtime's registries, task admission and explicitly
// declared service executors. A failed setup leaves the product unbootable.
bool setup_core_runtime(KernelRuntime &runtime,
                        std::initializer_list<ServiceExecutorDefinition> executors,
                        std::string &error);
bool setup_core_runtime(KernelRuntime &, const ServiceExecutorDefinition *, size_t,
                        std::string &error);

} // namespace lunaredu::lunaros
