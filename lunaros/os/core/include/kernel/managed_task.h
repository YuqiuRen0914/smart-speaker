#pragma once
#include "diagnostics/diagnostic_read_cost.h"
#include "kernel/execution_admission.h"
#include "kernel/registration_storage.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif

struct tskTaskControlBlock;

namespace lunaredu::lunaros {

inline constexpr size_t kManagedTaskIdBytes = 32;
inline constexpr size_t kManagedTaskOwnerBytes = 32;
inline constexpr size_t kManagedTaskRoleBytes = 24;

#ifdef CONFIG_FREERTOS_NUMBER_OF_CORES
inline constexpr int32_t kManagedTaskCoreCount = CONFIG_FREERTOS_NUMBER_OF_CORES;
#else
inline constexpr int32_t kManagedTaskCoreCount = 2;
#endif
#ifdef CONFIG_LUNAREDU_LUNAROS_DEFAULT_TASK_CORE
inline constexpr int32_t kManagedTaskDefaultCore = CONFIG_LUNAREDU_LUNAROS_DEFAULT_TASK_CORE;
#else
inline constexpr int32_t kManagedTaskDefaultCore = kManagedTaskCoreCount > 1 ? 1 : 0;
#endif
inline constexpr int32_t kManagedTaskIoCore = 0;
static_assert(kManagedTaskDefaultCore >= 0 && kManagedTaskDefaultCore < kManagedTaskCoreCount);
constexpr bool valid_managed_task_core(int32_t core) {
  return core >= 0 && core < kManagedTaskCoreCount;
}

enum class ManagedTaskLifetime : uint8_t {
  Resident,
  Feature,
  Transient,
};

enum class ManagedTaskMemory : uint8_t {
  Internal,
  External,
  // Internal, byte-addressable DMA RAM; excludes the S31 RTC heap.
  InternalDma,
};

enum class ManagedTaskOwnerKind : uint8_t { Service, Application, Subsystem };
constexpr const char *managed_task_owner_kind_name(ManagedTaskOwnerKind kind) {
  switch (kind) {
    case ManagedTaskOwnerKind::Service: return "service";
    case ManagedTaskOwnerKind::Application: return "application";
    case ManagedTaskOwnerKind::Subsystem: return "subsystem";
  }
  return "unknown";
}

struct ManagedTaskDefinition {
  const char *id = nullptr;
  const char *owner_service_id = nullptr;
  const char *role = nullptr;
  uint32_t stack_bytes = 0;
  uint32_t priority = 0;
  // Fixed default. Any-core placement additionally requires the explicit
  // allow_core_migration opt-in and cannot carry a Deadline contract.
  int32_t core_affinity = kManagedTaskDefaultCore;
  ManagedTaskLifetime lifetime = ManagedTaskLifetime::Resident;
  ManagedTaskMemory memory = ManagedTaskMemory::Internal;
  bool allow_internal_fallback = false;
  // owner_service_id is the owner identifier; owner_kind defines its namespace.
  ManagedTaskOwnerKind owner_kind = ManagedTaskOwnerKind::Service;
  // The trusted task definition supplies priority/core; admission overrides
  // those two contract fields with this task's actual scheduling attributes.
  ExecutionContract execution{};
  bool allow_core_migration = false;
};

struct ManagedTaskEntrySnapshot {
  bool active = false;
  uint64_t token = 0;
  std::array<char, kManagedTaskIdBytes> id{};
  std::array<char, kManagedTaskOwnerBytes> owner_service_id{};
  std::array<char, kManagedTaskRoleBytes> role{};
  uint32_t stack_bytes = 0;
  uint32_t priority = 0;
  int32_t core_affinity = -1;
  ManagedTaskLifetime lifetime = ManagedTaskLifetime::Resident;
  ManagedTaskMemory memory = ManagedTaskMemory::Internal;
  bool allow_internal_fallback = false;
  uintptr_t native_handle = 0;
  uint32_t minimum_free_stack_bytes = 0;
  ManagedTaskOwnerKind owner_kind = ManagedTaskOwnerKind::Service;
};

// Owned declaration facts. Handles are identity values only, never live task
// references; this observation performs no SDK access or stack measurement.
struct ManagedTaskMetadata {
  bool active = false;
  uint64_t token = 0;
  std::array<char, kManagedTaskIdBytes> id{};
  std::array<char, kManagedTaskOwnerBytes> owner_service_id{};
  std::array<char, kManagedTaskRoleBytes> role{};
  uint32_t stack_bytes = 0;
  uint32_t priority = 0;
  int32_t core_affinity = -1;
  ManagedTaskLifetime lifetime = ManagedTaskLifetime::Resident;
  ManagedTaskMemory memory = ManagedTaskMemory::Internal;
  bool allow_internal_fallback = false;
  uintptr_t native_handle = 0;
  ManagedTaskOwnerKind owner_kind = ManagedTaskOwnerKind::Service;
};

struct ManagedTaskMetadataSnapshot {
  uint32_t revision = 0;
  size_t observed_tasks = 0;
  bool complete = true;
  RegistrationArray<ManagedTaskMetadata> tasks{true};

  // For correlation across a native capture, check the registry revision after
  // that capture. A changing admission can reuse a handle; never annotate it
  // with stale ownership. The returned facts belong to this snapshot.
  const ManagedTaskMetadata *find(uintptr_t handle, uint32_t current_revision) const {
    if (!handle || current_revision != revision) return nullptr;
    for (const auto &task : tasks)
      if (task.active && task.native_handle == handle) return &task;
    return nullptr;
  }
};

// Admission reservations, including tasks awaiting activation or retirement.
// No native task access, stack scans, entries, or dynamically owned storage.
struct ManagedTaskStorageSnapshot {
  const char *memory = "none";
  size_t capacity = 0;
  size_t storage_bytes = 0;
};

struct ManagedTaskSummary {
  size_t active_tasks = 0;
  size_t resident_tasks = 0;
  size_t feature_tasks = 0;
  size_t transient_tasks = 0;
  size_t internal_stack_bytes = 0;
  size_t external_stack_bytes = 0;
  uint32_t revision = 0;
  ManagedTaskStorageSnapshot storage;
};

struct ManagedTaskRegistrySnapshot {
  system::DiagnosticReadCost read_cost;
  size_t active_tasks = 0;
  size_t resident_tasks = 0;
  size_t feature_tasks = 0;
  size_t transient_tasks = 0;
  size_t internal_stack_bytes = 0;
  size_t external_stack_bytes = 0;
  uint32_t revision = 0;
  ManagedTaskStorageSnapshot storage;
  bool complete = true;
  RegistrationArray<ManagedTaskEntrySnapshot> tasks{true};
};

const char *managed_task_lifetime_name(ManagedTaskLifetime lifetime);
const char *managed_task_memory_name(ManagedTaskMemory memory);

class KernelRuntime;

using ManagedTaskFunction = void (*)(void *);
using ManagedTaskHandle = tskTaskControlBlock *;

bool create_managed_task(KernelRuntime &runtime,
                         const ManagedTaskDefinition &definition,
                         ManagedTaskFunction function, void *context,
                         ManagedTaskHandle *handle, std::string &error);

}  // namespace lunaredu::lunaros
