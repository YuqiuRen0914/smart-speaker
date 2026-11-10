#include "lunaredu_lunaros/kernel/runtime.h"

#include "lunaredu_lunaros/kernel/service_executor.h"
#include "lunaredu_lunaros/kernel/task_registry.h"
#include "kernel/services/service_registry_internal.h"

#ifndef CONFIG_LUNAREDU_LUNAROS_MAX_MANAGED_TASKS
#define CONFIG_LUNAREDU_LUNAROS_MAX_MANAGED_TASKS 20
#endif
#ifndef CONFIG_LUNAREDU_LUNAROS_MAX_TRANSIENT_TASKS
#define CONFIG_LUNAREDU_LUNAROS_MAX_TRANSIENT_TASKS 4
#endif
#ifndef CONFIG_LUNAREDU_LUNAROS_MAX_RESIDENT_TASKS
#define CONFIG_LUNAREDU_LUNAROS_MAX_RESIDENT_TASKS 12
#endif
#ifndef CONFIG_LUNAREDU_LUNAROS_MAX_TASKS_PER_OWNER
#define CONFIG_LUNAREDU_LUNAROS_MAX_TASKS_PER_OWNER 6
#endif
#ifndef CONFIG_LUNAREDU_LUNAROS_MAX_INTERNAL_TASK_STACK_BYTES
#define CONFIG_LUNAREDU_LUNAROS_MAX_INTERNAL_TASK_STACK_BYTES (64 * 1024)
#endif
#ifndef CONFIG_LUNAREDU_LUNAROS_MAX_EXTERNAL_TASK_STACK_BYTES
#define CONFIG_LUNAREDU_LUNAROS_MAX_EXTERNAL_TASK_STACK_BYTES (192 * 1024)
#endif

namespace lunaredu::lunaros {

bool setup_core_runtime(std::initializer_list<ServiceExecutorDefinition> executors,
                        std::string &error) {
  error.clear();
  const ManagedTaskBudget task_budget = {
      .maximum_active_tasks = CONFIG_LUNAREDU_LUNAROS_MAX_MANAGED_TASKS,
      .maximum_transient_tasks = CONFIG_LUNAREDU_LUNAROS_MAX_TRANSIENT_TASKS,
      .maximum_internal_stack_bytes =
          CONFIG_LUNAREDU_LUNAROS_MAX_INTERNAL_TASK_STACK_BYTES,
      .maximum_external_stack_bytes =
          CONFIG_LUNAREDU_LUNAROS_MAX_EXTERNAL_TASK_STACK_BYTES,
      .maximum_resident_tasks = CONFIG_LUNAREDU_LUNAROS_MAX_RESIDENT_TASKS,
      .maximum_tasks_per_owner = CONFIG_LUNAREDU_LUNAROS_MAX_TASKS_PER_OWNER,
  };
  if (!setup_managed_task_registry(task_budget, error)) {
    return false;
  }
  if (!setup_service_executors(executors, &error)) {
    return false;
  }
  if (!service_registry_internal().initialize(error)) {
    return false;
  }
  reset_service_registry_internal();
  return true;
}

} // namespace lunaredu::lunaros
