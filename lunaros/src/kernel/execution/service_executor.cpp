#include "lunaredu_lunaros/kernel/service_executor.h"


#include <atomic>
#include <cstring>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

namespace lunaredu::lunaros {
namespace {

struct ServiceInvocation {
  ServiceOperation operation = nullptr;
  void *context = nullptr;
  ServiceOperationResult *result = nullptr;
  SemaphoreHandle_t completion = nullptr;
};

struct ExecutorState {
  char id[24] = {0};
  char owner_service_id[32] = {0};
  QueueHandle_t queue = nullptr;
  std::atomic<uintptr_t> task_handle{0};
  uint8_t queue_depth = 0;
};

constexpr size_t kExecutorCapacity = 4;
ExecutorState g_executors[kExecutorCapacity];
uint8_t g_executor_count = 0;

ExecutorState *find_executor(const char *id) {
  if (id == nullptr || id[0] == '\0') {
    return nullptr;
  }
  for (uint8_t index = 0; index < g_executor_count; ++index) {
    if (std::strcmp(g_executors[index].id, id) == 0) {
      return &g_executors[index];
    }
  }
  return nullptr;
}

void set_error(std::string *error, const char *message) {
  if (error != nullptr) {
    *error = message;
  }
}

void execute(ServiceInvocation &invocation) {
  if (invocation.result == nullptr) {
    return;
  }
  invocation.result->ok = true;
  invocation.result->error.clear();
  if (invocation.operation == nullptr) {
    invocation.result->ok = false;
    invocation.result->error = "invalid_service_operation";
    return;
  }
  invocation.operation(invocation.context, *invocation.result);
}

} // namespace

bool setup_service_executors(std::initializer_list<ServiceExecutorDefinition> definitions,
                             std::string *error) {
  if (error) error->clear();
  if (definitions.size() == 0 || definitions.size() > kExecutorCapacity) {
    set_error(error, "invalid_service_executor_count");
    return false;
  }
  // Validate the complete declaration before allocating or publishing anything.
  for (auto current = definitions.begin(); current != definitions.end(); ++current) {
    if (!current->id || !*current->id || !current->owner_service_id ||
        !*current->owner_service_id || !current->queue_depth ||
        std::strlen(current->id) >= sizeof(g_executors[0].id) ||
        std::strlen(current->owner_service_id) >= sizeof(g_executors[0].owner_service_id)) {
      set_error(error, "invalid_service_executor_definition");
      return false;
    }
    for (auto previous = definitions.begin(); previous != current; ++previous) {
      if (std::strcmp(previous->id, current->id) == 0) {
        set_error(error, "duplicate_service_executor");
        return false;
      }
    }
  }
  if (g_executor_count) {
    bool same = g_executor_count == definitions.size();
    for (const auto &definition : definitions) {
      auto *existing = find_executor(definition.id);
      same = same && existing && existing->queue_depth == definition.queue_depth &&
          std::strcmp(existing->owner_service_id, definition.owner_service_id) == 0;
    }
    if (!same) set_error(error, "service_executor_registry_already_configured");
    return same;
  }
  for (const auto &definition : definitions) {
    if (!register_service_executor(definition, error)) {
      for (uint8_t index = 0; index < g_executor_count; ++index) {
        vQueueDelete(g_executors[index].queue);
        g_executors[index].queue = nullptr;
        g_executors[index].id[0] = '\0';
      }
      g_executor_count = 0;
      return false;
    }
  }
  return true;
}

bool register_service_executor(const ServiceExecutorDefinition &definition,
                               std::string *error) {
  if (error != nullptr) {
    error->clear();
  }
  if (definition.id == nullptr || definition.id[0] == '\0' ||
      definition.owner_service_id == nullptr ||
      definition.owner_service_id[0] == '\0' || definition.queue_depth == 0) {
    set_error(error, "invalid_service_executor");
    return false;
  }
  if (std::strlen(definition.id) >= sizeof(g_executors[0].id) ||
      std::strlen(definition.owner_service_id) >=
          sizeof(g_executors[0].owner_service_id)) {
    set_error(error, "service_executor_metadata_too_long");
    return false;
  }
  if (find_executor(definition.id) != nullptr) {
    set_error(error, "service_executor_already_registered");
    return false;
  }
  if (g_executor_count >= kExecutorCapacity) {
    set_error(error, "service_executor_registry_full");
    return false;
  }

  QueueHandle_t queue =
      xQueueCreate(definition.queue_depth, sizeof(ServiceInvocation));
  if (queue == nullptr) {
    set_error(error, "service_executor_queue_allocation_failed");
    return false;
  }

  ExecutorState &executor = g_executors[g_executor_count++];
  std::memcpy(executor.id, definition.id, std::strlen(definition.id) + 1);
  std::memcpy(executor.owner_service_id, definition.owner_service_id,
              std::strlen(definition.owner_service_id) + 1);
  executor.queue = queue;
  executor.queue_depth = definition.queue_depth;
  return true;
}

bool bind_service_executor_to_current_task(const char *executor_id,
                                           std::string *error) {
  if (error != nullptr) {
    error->clear();
  }
  ExecutorState *executor = find_executor(executor_id);
  if (executor == nullptr) {
    set_error(error, "service_executor_not_found");
    return false;
  }

  const uintptr_t current =
      reinterpret_cast<uintptr_t>(xTaskGetCurrentTaskHandle());
  uintptr_t existing = 0;
  if (!executor->task_handle.compare_exchange_strong(
          existing, current, std::memory_order_acq_rel) &&
      existing != current) {
    set_error(error, "service_executor_already_bound");
    return false;
  }
  return true;
}

bool unbind_service_executor_from_current_task(const char *executor_id,
                                               std::string *error) {
  auto *executor = find_executor(executor_id);
  if (!executor || uxQueueMessagesWaiting(executor->queue) != 0) {
    set_error(error, "service_executor_not_quiescent");
    return false;
  }
  uintptr_t owner = reinterpret_cast<uintptr_t>(xTaskGetCurrentTaskHandle());
  if (!executor->task_handle.compare_exchange_strong(owner, 0)) {
    set_error(error, "service_executor_wrong_owner_task");
    return false;
  }
  if (error)
    error->clear();
  return true;
}

bool pump_service_executor(const char *executor_id, uint32_t wait_timeout_ms,
                           std::string *error) {
  if (error != nullptr) {
    error->clear();
  }
  ExecutorState *executor = find_executor(executor_id);
  if (executor == nullptr || executor->queue == nullptr) {
    set_error(error, "service_executor_unavailable");
    return false;
  }
  const uintptr_t current =
      reinterpret_cast<uintptr_t>(xTaskGetCurrentTaskHandle());
  const uintptr_t owner =
      executor->task_handle.load(std::memory_order_acquire);
  if (owner == 0) {
    if (!bind_service_executor_to_current_task(executor_id, error)) {
      return false;
    }
  } else if (owner != current) {
    set_error(error, "service_executor_wrong_owner_task");
    return false;
  }

  const TickType_t wait_ticks =
      wait_timeout_ms == UINT32_MAX ? portMAX_DELAY
                                    : pdMS_TO_TICKS(wait_timeout_ms);
  if (uxQueueMessagesWaiting(executor->queue) == 0) {
    ulTaskNotifyTake(pdTRUE, wait_ticks);
  }

  ServiceInvocation invocation;
  // Arrivals during callbacks belong to a later round. A bounded queue alone
  // does not bound an unbounded drain loop under continuous producers.
  const UBaseType_t pending = uxQueueMessagesWaiting(executor->queue);
  for (UBaseType_t index = 0; index < pending &&
       xQueueReceive(executor->queue, &invocation, 0) == pdPASS; ++index) {
    execute(invocation);
    if (invocation.completion != nullptr) {
      (void)xSemaphoreGive(invocation.completion);
    }
  }
  return true;
}

size_t service_executor_snapshot(ServiceExecutorSnapshot *output,
                                 size_t capacity) {
  if (output == nullptr || capacity == 0) {
    return g_executor_count;
  }
  const size_t count = capacity < g_executor_count ? capacity : g_executor_count;
  for (size_t index = 0; index < count; ++index) {
    const ExecutorState &executor = g_executors[index];
    output[index] = {
        .id = executor.id,
        .owner_service_id = executor.owner_service_id,
        .queue_depth = executor.queue_depth,
        .bound = executor.task_handle.load(std::memory_order_acquire) != 0,
    };
  }
  return g_executor_count;
}

bool current_task_owns_service_executor() {
  const uintptr_t current =
      reinterpret_cast<uintptr_t>(xTaskGetCurrentTaskHandle());
  for (uint8_t index = 0; index < g_executor_count; ++index) {
    if (current != 0 && g_executors[index].task_handle.load(
                            std::memory_order_acquire) == current) return true;
  }
  return false;
}

bool invoke_service_operation(const char *executor_id,
                              ServiceOperation operation, void *context,
                              std::string &error,
                              uint32_t admission_timeout_ms) {
  error.clear();
  ExecutorState *executor = find_executor(executor_id);
  if (executor == nullptr || executor->queue == nullptr) {
    error = "service_executor_unavailable";
    return false;
  }
  if (operation == nullptr) {
    error = "invalid_service_operation";
    return false;
  }
  ServiceOperationResult result;
  ServiceInvocation invocation{operation, context, &result, nullptr};
  const uintptr_t owner =
      executor->task_handle.load(std::memory_order_acquire);
  const uintptr_t current =
      reinterpret_cast<uintptr_t>(xTaskGetCurrentTaskHandle());
  if (owner != 0 && current == owner) {
    execute(invocation);
  } else {
    if (owner == 0) {
      error = "service_executor_unbound";
      return false;
    }
    StaticSemaphore_t completion_storage{};
    SemaphoreHandle_t completion =
        xSemaphoreCreateBinaryStatic(&completion_storage);
    if (completion == nullptr) {
      error = "service_executor_completion_allocation_failed";
      return false;
    }
    invocation.completion = completion;
    const TickType_t admission_ticks =
        admission_timeout_ms == UINT32_MAX
            ? portMAX_DELAY
            : pdMS_TO_TICKS(admission_timeout_ms);
    if (xQueueSend(executor->queue, &invocation, admission_ticks) != pdPASS) {
      error = "service_executor_queue_full";
      return false;
    }
    xTaskNotifyGive(reinterpret_cast<TaskHandle_t>(owner));
    if (xSemaphoreTake(completion, portMAX_DELAY) != pdTRUE) {
      error = "service_executor_completion_wait_failed";
      return false;
    }
  }

  if (!result.ok) {
    error = result.error.empty() ? "service_operation_failed" : result.error;
  }
  return result.ok;
}

} // namespace lunaredu::lunaros
