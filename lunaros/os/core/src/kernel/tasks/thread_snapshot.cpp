#include "kernel/tasks/thread_snapshot.h"
#include <type_traits>
#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/freertos_debug.h>
#if !CONFIG_FREERTOS_SMP && CONFIG_FREERTOS_NUMBER_OF_CORES > 1
// Isolate the ESP-IDF kernel adapter here. Its exported wrapper protects the
// actual task lists, unlike a new application spinlock or caller-only pause.
#include <esp_private/freertos_idf_additions_priv.h>
#endif
#include <esp_memory_utils.h>
#if configGENERATE_RUN_TIME_STATS
#include <freertos/idf_additions.h>
#endif
#endif

#if defined(ESP_PLATFORM) && configUSE_TRACE_FACILITY && \
    (!CONFIG_FREERTOS_SMP || CONFIG_FREERTOS_NUMBER_OF_CORES == 1)
#define LUNAROS_NATIVE_CAPTURE_SUPPORTED 1
#else
#define LUNAROS_NATIVE_CAPTURE_SUPPORTED 0
#endif

namespace lunaredu::lunaros {
namespace {
#if LUNAROS_NATIVE_CAPTURE_SUPPORTED
class NativeTaskCaptureGuard {
public:
  NativeTaskCaptureGuard() {
#if CONFIG_FREERTOS_NUMBER_OF_CORES > 1
    prvTakeKernelLock();
#else
    vTaskSuspendAll();
#endif
  }
  ~NativeTaskCaptureGuard() {
#if CONFIG_FREERTOS_NUMBER_OF_CORES > 1
    prvReleaseKernelLock();
#else
    xTaskResumeAll();
#endif
  }
  NativeTaskCaptureGuard(const NativeTaskCaptureGuard &) = delete;
  NativeTaskCaptureGuard &operator=(const NativeTaskCaptureGuard &) = delete;
};
void copy_task_name(std::array<char, 32> &target, const char *source) {
  // No libc formatting, allocation or other owner lock under the kernel lock.
  size_t i = 0;
  if (source) while (i + 1 < target.size() && source[i]) {
    target[i] = source[i]; ++i;
  }
  target[i] = '\0';
}
#endif
template<class Snapshot> void clear_snapshot(Snapshot &result) {
  result.available = false; result.complete = false; result.core_affinity_available = false;
  result.observed_threads = result.captured_threads = 0; result.error.clear();
  result.runtime_counter_bits = 0;
  if constexpr(requires{result.total_runtime_counter;}){result.total_runtime_counter=0;result.total_counter_available=false;}
  for (auto &thread : result.threads) thread = {};
}
}
template<class Snapshot> void capture_native_thread_inventory(Snapshot &result) {
  clear_snapshot(result);
#if LUNAROS_NATIVE_CAPTURE_SUPPORTED
  const bool external=heap_caps_get_total_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)!=0;
  auto *tasks = static_cast<TaskStatus_t *>(heap_caps_calloc(
      kNativeThreadSnapshotCapacity, sizeof(TaskStatus_t),
      MALLOC_CAP_8BIT|(external?MALLOC_CAP_SPIRAM:MALLOC_CAP_INTERNAL)));
  if (!tasks) { result.error = "thread_snapshot_allocation_failed"; return; }
  result.available = true;
#if configGENERATE_RUN_TIME_STATS
  result.runtime_counter_bits = sizeof(configRUN_TIME_COUNTER_TYPE) * 8;
#endif
#if configTASKLIST_INCLUDE_COREID
  result.core_affinity_available = true;
#endif
  // Keep borrowed names/handles alive through copying and stack inspection.
  // Allocate before taking the kernel guard; free after releasing it.
  {
    NativeTaskCaptureGuard guard;
    result.observed_threads = uxTaskGetNumberOfTasks();
    result.captured_threads = uxTaskGetSystemState(tasks, kNativeThreadSnapshotCapacity, nullptr);
    for (size_t i = 0; i < result.captured_threads; ++i) {
      const auto &source = tasks[i]; auto &target = result.threads[i];
      copy_task_name(target.name, source.pcTaskName);
      target.handle = reinterpret_cast<uintptr_t>(source.xHandle);
      target.number = source.xTaskNumber; target.priority = source.uxCurrentPriority;
      if constexpr(std::is_same_v<Snapshot,NativeThreadRegistrySnapshot>) {
      target.minimum_free_stack_bytes = source.usStackHighWaterMark;
      TaskSnapshot_t stack{};
      if (source.pxStackBase && vTaskGetSnapshot(source.xHandle, &stack) == pdTRUE) {
        const uintptr_t base = reinterpret_cast<uintptr_t>(source.pxStackBase);
        const uintptr_t end = reinterpret_cast<uintptr_t>(stack.pxEndOfStack);
        if (end >= base && end - base < 1024 * 1024) {
          target.stack_span_bytes = static_cast<uint32_t>(end - base + sizeof(StackType_t));
          target.stack_start = base;
        }
        target.stack_memory = esp_ptr_external_ram(source.pxStackBase) ? "external"
                            : esp_ptr_internal(source.pxStackBase) ? "internal" : "unknown";
      }
      }
#if configGENERATE_RUN_TIME_STATS
      target.runtime_counter = source.ulRunTimeCounter;
      if (source.xHandle == xTaskGetIdleTaskHandleForCore(0)) target.idle_core = 0;
#if CONFIG_FREERTOS_NUMBER_OF_CORES > 1
      if (source.xHandle == xTaskGetIdleTaskHandleForCore(1)) target.idle_core = 1;
#endif
#endif
#if configTASKLIST_INCLUDE_COREID
      target.core_affinity = source.xCoreID == tskNO_AFFINITY ? -1 : source.xCoreID;
#endif
      if constexpr(std::is_same_v<Snapshot,NativeThreadRegistrySnapshot>) switch (source.eCurrentState) {
        case eRunning: target.state = "running"; break;
        case eReady: target.state = "ready"; break;
        case eBlocked: target.state = "blocked"; break;
        case eSuspended: target.state = "suspended"; break;
        case eDeleted: target.state = "deleted"; break;
        default: break;
      }
    }
  }
  heap_caps_free(tasks);
  result.complete = result.captured_threads != 0 && result.captured_threads == result.observed_threads;
  if (!result.complete) result.error = "thread_snapshot_incomplete";
#elif defined(ESP_PLATFORM) && configUSE_TRACE_FACILITY
  result.error = "thread_snapshot_kernel_unsupported";
#else
  result.error = "freertos_trace_unavailable";
#endif
}
void capture_native_threads(NativeThreadRegistrySnapshot &result) {capture_native_thread_inventory(result);}
void capture_native_thread_counters_fallback(NativeThreadCounterRegistrySnapshot &result) {capture_native_thread_inventory(result);}
template<class Snapshot> void capture_thread_counters(Snapshot &result) {
  clear_snapshot(result);
#if LUNAROS_NATIVE_CAPTURE_SUPPORTED
  result.available = true;
#if configGENERATE_RUN_TIME_STATS
  result.runtime_counter_bits = sizeof(configRUN_TIME_COUNTER_TYPE) * 8;
#endif
#if configTASKLIST_INCLUDE_COREID
  result.core_affinity_available = true;
#endif
  // Guard the SDK task lists for the whole traversal, including borrowed TCB
  // reads and name copying. Caller-only suspension does not protect dual core.
  {
    NativeTaskCaptureGuard guard;
    result.observed_threads = uxTaskGetNumberOfTasks();
#if configGENERATE_RUN_TIME_STATS
    if constexpr(requires{result.total_runtime_counter;}){
#ifdef portALT_GET_RUN_TIME_COUNTER_VALUE
    configRUN_TIME_COUNTER_TYPE total=0;portALT_GET_RUN_TIME_COUNTER_VALUE(total);
    result.total_runtime_counter=total;result.total_counter_available=true;
#elif defined(portGET_RUN_TIME_COUNTER_VALUE)
    result.total_runtime_counter=portGET_RUN_TIME_COUNTER_VALUE();result.total_counter_available=true;
#endif
    }
#endif
    TaskIterator_t iterator{};
    while (result.captured_threads < result.threads.size() &&
           xTaskGetNext(&iterator) != -1) {
      // NULL is an invalid iterator entry, not a request for the current task.
      if (!iterator.pxTaskHandle) break;
      TaskStatus_t source{};
      vTaskGetInfo(iterator.pxTaskHandle, &source, pdFALSE, eInvalid);
      auto &target = result.threads[result.captured_threads++];
      copy_task_name(target.name, source.pcTaskName);
      target.handle = reinterpret_cast<uintptr_t>(source.xHandle);
      target.number = source.xTaskNumber; target.priority = source.uxCurrentPriority;
#if configGENERATE_RUN_TIME_STATS
      target.runtime_counter = source.ulRunTimeCounter;
      if (source.xHandle == xTaskGetIdleTaskHandleForCore(0)) target.idle_core = 0;
#if CONFIG_FREERTOS_NUMBER_OF_CORES > 1
      if (source.xHandle == xTaskGetIdleTaskHandleForCore(1)) target.idle_core = 1;
#endif
#endif
#if configTASKLIST_INCLUDE_COREID
      target.core_affinity = source.xCoreID == tskNO_AFFINITY ? -1 : source.xCoreID;
#endif

    }
  }
  // Validate even a same-count malformed observation; never publish duplicated
  // identities as valid usage. All borrowed SDK pointers are already retired.
  bool identities_unique=true;
  for(size_t i=0;i<result.captured_threads;++i){
    if(!result.threads[i].handle){identities_unique=false;break;}
    for(size_t j=0;j<i;++j)
      if(result.threads[i].handle==result.threads[j].handle||
         result.threads[i].number==result.threads[j].number)identities_unique=false;
  }
  result.complete = identities_unique && result.captured_threads != 0 && result.captured_threads == result.observed_threads;
  if (!result.complete) result.error = "thread_snapshot_incomplete";
#elif defined(ESP_PLATFORM) && configUSE_TRACE_FACILITY
  result.error = "thread_snapshot_kernel_unsupported";
#else
  result.error = "freertos_trace_unavailable";
#endif
}
void capture_native_thread_counters(NativeThreadCounterRegistrySnapshot &result){capture_thread_counters(result);}
NativeThreadCounterFacts capture_native_thread_counters(std::span<NativeThreadCounterSnapshot> output){
  if(output.empty()||output.size()>kNativeThreadCounterReadCapacity){
    NativeThreadCounterFacts result;result.error="thread_snapshot_capacity_invalid";return result;
  }
  struct Borrowed:NativeThreadCounterFacts {std::span<NativeThreadCounterSnapshot> threads;};
  Borrowed result;result.threads=output;capture_thread_counters(result);return result;
}

}
