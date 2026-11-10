#include "lunaredu_lunaros/kernel/managed_task.h"

#include "kernel/tasks/task_registry_internal.h"

#include <new>

#include <esp_log.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/idf_additions.h>
#include <freertos/task.h>

namespace lunaredu::lunaros {
namespace {

struct ManagedTaskStartContext {
  ManagedTaskAdmission admission;
  ManagedTaskFunction function = nullptr;
  void *context = nullptr;
  ManagedTaskMemory memory = ManagedTaskMemory::Internal;
};

void managed_task_entry(void *opaque) {
  auto *start = static_cast<ManagedTaskStartContext *>(opaque);
  const ManagedTaskAdmission admission = start->admission;
  const ManagedTaskFunction function = start->function;
  void *context = start->context;
  const ManagedTaskMemory memory = start->memory;
  delete start;

  std::string activation_error;
  const bool activated = managed_task_registry_internal().activate(
      admission, reinterpret_cast<uintptr_t>(xTaskGetCurrentTaskHandle()),
      activation_error);
  if (activated) {
    function(context);
  } else {
    // Never run work outside the registry's accounting and ownership model.
    // This should only be possible after registry corruption or an invalid
    // admission lifecycle, but continuing would create an invisible task that
    // cannot be diagnosed or bounded by the configured task budget.
    ESP_LOGE("managed_task", "activation failed: %s",
             activation_error.c_str());
  }
  (void)managed_task_registry_internal().release(admission);
  if (memory == ManagedTaskMemory::External) {
    vTaskDeleteWithCaps(nullptr);
  } else {
    vTaskDelete(nullptr);
  }
}

BaseType_t create_task_with_memory(const ManagedTaskDefinition &definition,
                                   ManagedTaskStartContext *start,
                                   TaskHandle_t *handle) {
  if (definition.memory == ManagedTaskMemory::External) {
    if (definition.core_affinity < 0) {
      return xTaskCreateWithCaps(
          managed_task_entry, definition.id, definition.stack_bytes, start,
          definition.priority, handle,
          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    return xTaskCreatePinnedToCoreWithCaps(
        managed_task_entry, definition.id, definition.stack_bytes, start,
        definition.priority, handle, definition.core_affinity,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  }
  if (definition.core_affinity < 0) {
    return xTaskCreate(managed_task_entry, definition.id,
                       definition.stack_bytes, start, definition.priority,
                       handle);
  }
  return xTaskCreatePinnedToCore(managed_task_entry, definition.id,
                                 definition.stack_bytes, start,
                                 definition.priority, handle,
                                 definition.core_affinity);
}

}  // namespace

bool create_managed_task(const ManagedTaskDefinition &definition,
                         ManagedTaskFunction function, void *context,
                         ManagedTaskHandle *handle, std::string &error) {
  error.clear();
  if (function == nullptr) {
    error = "managed_task_missing_function";
    return false;
  }

  ManagedTaskAdmission admission =
      managed_task_registry_internal().admit(definition, error);
  if (!admission) {
    return false;
  }

  auto *start = new (std::nothrow)
      ManagedTaskStartContext{admission, function, context, definition.memory};
  if (start == nullptr) {
    (void)managed_task_registry_internal().release(admission);
    error = "managed_task_context_allocation_failed";
    return false;
  }

  TaskHandle_t created_handle = nullptr;
  TaskHandle_t *target_handle = handle != nullptr ? handle : &created_handle;
  BaseType_t created = create_task_with_memory(definition, start, target_handle);
  if (created != pdPASS && definition.memory == ManagedTaskMemory::External &&
      definition.allow_internal_fallback) {
    std::string fallback_error;
    if (managed_task_registry_internal().retarget_memory(
            admission, ManagedTaskMemory::Internal, fallback_error)) {
      ManagedTaskDefinition fallback = definition;
      fallback.memory = ManagedTaskMemory::Internal;
      start->memory = ManagedTaskMemory::Internal;
      created = create_task_with_memory(fallback, start, target_handle);
    } else {
      error = fallback_error;
    }
  }

  if (created != pdPASS) {
    delete start;
    (void)managed_task_registry_internal().release(admission);
    if (error.empty()) {
      error = "managed_task_create_failed";
    }
    if (handle != nullptr) {
      *handle = nullptr;
    }
    return false;
  }
  return true;
}

}  // namespace lunaredu::lunaros
