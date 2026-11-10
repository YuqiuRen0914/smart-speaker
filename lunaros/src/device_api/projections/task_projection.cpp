#include "lunaredu_lunaros/device_api/standard_projections.h"
#include <ArduinoJson.h>

#include "lunaredu_lunaros/device_api/device_projection.h"
#include "lunaredu_lunaros/kernel/log.h"
#include "lunaredu_lunaros/kernel/service_executor.h"
#include "lunaredu_lunaros/kernel/task_registry.h"

namespace lunaredu::lunaros {
namespace {

constexpr const char *kModuleId = "runtime";

void fill_task_status(JsonObject payload) {
  const ManagedTaskRegistrySnapshot snapshot = managed_task_registry_snapshot();
  payload["revision"] = snapshot.revision;
  payload["active_tasks"] = snapshot.active_tasks;
  payload["resident_tasks"] = snapshot.resident_tasks;
  payload["feature_tasks"] = snapshot.feature_tasks;
  payload["transient_tasks"] = snapshot.transient_tasks;
  payload["internal_stack_bytes"] = snapshot.internal_stack_bytes;
  payload["external_stack_bytes"] = snapshot.external_stack_bytes;

  JsonObject budget = payload["budget"].to<JsonObject>();
  budget["maximum_active_tasks"] = snapshot.budget.maximum_active_tasks;
  budget["maximum_transient_tasks"] = snapshot.budget.maximum_transient_tasks;
  budget["maximum_resident_tasks"] = snapshot.budget.maximum_resident_tasks;
  budget["maximum_tasks_per_owner"] =
      snapshot.budget.maximum_tasks_per_owner;
  budget["maximum_internal_stack_bytes"] =
      snapshot.budget.maximum_internal_stack_bytes;
  budget["maximum_external_stack_bytes"] =
      snapshot.budget.maximum_external_stack_bytes;

  JsonArray tasks = payload["tasks"].to<JsonArray>();
  for (const ManagedTaskEntrySnapshot &task : snapshot.tasks) {
    if (!task.active) {
      continue;
    }
    JsonObject item = tasks.add<JsonObject>();
    item["id"] = task.id.data();
    item["owner_service"] = task.owner_service_id.data();
    item["role"] = task.role.data();
    item["stack_bytes"] = task.stack_bytes;
    item["priority"] = task.priority;
    item["core_affinity"] = task.core_affinity;
    item["lifetime"] = managed_task_lifetime_name(task.lifetime);
    item["memory"] = managed_task_memory_name(task.memory);
    item["internal_fallback"] = task.allow_internal_fallback;
    item["started"] = task.native_handle != 0;
  }

  ServiceExecutorSnapshot executor_snapshots[4] = {};
  const size_t executor_count = service_executor_snapshot(
      executor_snapshots,
      sizeof(executor_snapshots) / sizeof(executor_snapshots[0]));
  JsonArray executors = payload["executors"].to<JsonArray>();
  const size_t visible_executor_count =
      executor_count < 4 ? executor_count : 4;
  for (size_t index = 0; index < visible_executor_count; ++index) {
    const ServiceExecutorSnapshot &executor =
        executor_snapshots[index];
    JsonObject item = executors.add<JsonObject>();
    item["id"] = executor.id;
    item["owner_service"] = executor.owner_service_id;
    item["queue_depth"] = executor.queue_depth;
    item["bound"] = executor.bound;
  }
}

void fill_contract(JsonObject contract) {
  contract["registry_version"] = 1;
  contract["admission_control"] = true;
  contract["fixed_capacity"] = kManagedTaskCapacity;
  contract["stack_memory_accounting"] = true;
  contract["owner_service_required"] = true;
}

const DeviceCapabilityDescriptor kTaskRuntimeCapability = []() {
  DeviceCapabilityDescriptor descriptor;
  descriptor.id = "runtime_tasks";
  return descriptor;
}();

constexpr DeviceOperationDefinition kTaskOperations[] = {
    make_device_json_provider("tasks.get", fill_task_status),
};

const DeviceModuleProjectionMetadata kTaskModule = {
    kModuleId,
    "runtime",
    fill_contract,
};

const DeviceModuleProjection kTaskProjection = {
    "runtime_tasks",
    &kTaskModule,
    kTaskOperations,
    sizeof(kTaskOperations) / sizeof(kTaskOperations[0]),
};

}  // namespace

bool setup_task_projection(std::string &error) {
  return register_device_projection(
      {&kTaskRuntimeCapability, &kTaskProjection, nullptr, true}, &error);
}

}  // namespace lunaredu::lunaros
