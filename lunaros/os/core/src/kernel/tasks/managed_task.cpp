#include "kernel/runtime_internal.h"
#include "kernel/managed_task.h"

#include "kernel/runtime.h"
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
  ManagedTaskRegistry *registry = nullptr;
  ManagedTaskFunction function = nullptr;
  void *context = nullptr;
  ManagedTaskMemory memory = ManagedTaskMemory::Internal;
};

void managed_task_entry(void *opaque) {
  auto *start = static_cast<ManagedTaskStartContext *>(opaque);
  const ManagedTaskAdmission admission = start->admission;
  ManagedTaskRegistry *registry = start->registry;
  const ManagedTaskFunction function = start->function;
  void *context = start->context;
  const ManagedTaskMemory memory = start->memory;
  delete start;

  std::string activation_error;
  const bool activated = registry->activate(
      admission, reinterpret_cast<uintptr_t>(xTaskGetCurrentTaskHandle()),
      activation_error);
  if (activated) {
    function(context);
  } else {
    // Never run work outside the registry's accounting and ownership model.
    // This should only be possible after registry corruption or an invalid
    // admission lifecycle, but continuing would create an invisible task that
    // cannot be diagnosed or retired through its owner.
    ESP_LOGE("managed_task", "activation failed: %s",
             activation_error.c_str());
  }
  (void)registry->release(admission);
  if (memory != ManagedTaskMemory::Internal) {
    vTaskDeleteWithCaps(nullptr);
  } else {
    vTaskDelete(nullptr);
  }
}

BaseType_t create_task_with_memory(const ManagedTaskDefinition &definition,
                                   ManagedTaskStartContext *start,
                                   TaskHandle_t *handle) {
  const BaseType_t core=definition.core_affinity==kExecutionAnyCore?tskNO_AFFINITY:definition.core_affinity;
  if (definition.memory != ManagedTaskMemory::Internal) {
    const uint32_t caps = definition.memory == ManagedTaskMemory::External
                              ? MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
                              : MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT | MALLOC_CAP_DMA;
    return xTaskCreatePinnedToCoreWithCaps(
        managed_task_entry, definition.id, definition.stack_bytes, start,
        definition.priority, handle, core,
        caps);
  }
  return xTaskCreatePinnedToCore(managed_task_entry, definition.id,
                                 definition.stack_bytes, start,
                                 definition.priority, handle,
                                 core);
}

}  // namespace

bool create_managed_task(KernelRuntime &runtime,
                         const ManagedTaskDefinition &definition,
                         ManagedTaskFunction function, void *context,
                         ManagedTaskHandle *handle, std::string &error) {
  error.clear();
  if (handle != nullptr) *handle = nullptr;
  if (function == nullptr) {
    error = "managed_task_missing_function";
    return false;
  }
  if (runtime.task_ownership_enforced() && !runtime.validate_task_owner(definition, error)) return false;

  ManagedTaskAdmission admission =
      KernelRuntimeAccess::tasks(runtime).admit(definition, error);
  if (!admission) {
    return false;
  }

  auto *start = new (std::nothrow)
      ManagedTaskStartContext{admission, &KernelRuntimeAccess::tasks(runtime), function,
                              context, definition.memory};
  if (start == nullptr) {
    (void)KernelRuntimeAccess::tasks(runtime).release(admission);
    error = "managed_task_context_allocation_failed";
    return false;
  }

  TaskHandle_t created_handle = nullptr;
  TaskHandle_t *target_handle = handle != nullptr ? handle : &created_handle;
  BaseType_t created = create_task_with_memory(definition, start, target_handle);
  if (created != pdPASS && definition.memory == ManagedTaskMemory::External &&
      definition.allow_internal_fallback) {
    std::string fallback_error;
    if (KernelRuntimeAccess::tasks(runtime).retarget_memory(
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
    const uint32_t caps = start->memory == ManagedTaskMemory::External
        ? MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
        : MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT |
          (start->memory == ManagedTaskMemory::InternalDma ? MALLOC_CAP_DMA : 0);
    ESP_LOGE("managed_task", "create failed: id=%s owner=%s stack=%lu caps=%lu free=%u largest=%u",
             definition.id, definition.owner_service_id,
             static_cast<unsigned long>(definition.stack_bytes),
             static_cast<unsigned long>(caps),
             unsigned(heap_caps_get_free_size(caps)),
             unsigned(heap_caps_get_largest_free_block(caps)));
    delete start;
    (void)KernelRuntimeAccess::tasks(runtime).release(admission);
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
