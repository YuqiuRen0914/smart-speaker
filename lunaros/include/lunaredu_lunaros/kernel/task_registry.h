#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace lunaredu::lunaros {

inline constexpr size_t kManagedTaskCapacity = 24;
inline constexpr size_t kManagedTaskIdBytes = 32;
inline constexpr size_t kManagedTaskOwnerBytes = 32;
inline constexpr size_t kManagedTaskRoleBytes = 24;

enum class ManagedTaskLifetime : uint8_t {
  Resident,
  Feature,
  Transient,
};

enum class ManagedTaskMemory : uint8_t {
  Internal,
  External,
};

struct ManagedTaskBudget {
  size_t maximum_active_tasks = 20;
  size_t maximum_transient_tasks = 4;
  size_t maximum_internal_stack_bytes = 64 * 1024;
  size_t maximum_external_stack_bytes = 192 * 1024;
  size_t maximum_resident_tasks = 12;
  size_t maximum_tasks_per_owner = 6;
};

struct ManagedTaskDefinition {
  const char *id = nullptr;
  const char *owner_service_id = nullptr;
  const char *role = nullptr;
  uint32_t stack_bytes = 0;
  uint32_t priority = 0;
  int32_t core_affinity = -1;
  ManagedTaskLifetime lifetime = ManagedTaskLifetime::Resident;
  ManagedTaskMemory memory = ManagedTaskMemory::Internal;
  bool allow_internal_fallback = false;
};

struct ManagedTaskAdmission {
  uint32_t token = 0;
  size_t slot = kManagedTaskCapacity;

  explicit operator bool() const { return token != 0; }
};

struct ManagedTaskEntrySnapshot {
  bool active = false;
  uint32_t token = 0;
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
};

struct ManagedTaskRegistrySnapshot {
  ManagedTaskBudget budget;
  size_t active_tasks = 0;
  size_t resident_tasks = 0;
  size_t feature_tasks = 0;
  size_t transient_tasks = 0;
  size_t internal_stack_bytes = 0;
  size_t external_stack_bytes = 0;
  uint32_t revision = 0;
  std::array<ManagedTaskEntrySnapshot, kManagedTaskCapacity> tasks{};
};

bool setup_managed_task_registry(const ManagedTaskBudget &budget,
                                 std::string &error);
ManagedTaskRegistrySnapshot managed_task_registry_snapshot();

const char *managed_task_lifetime_name(ManagedTaskLifetime lifetime);
const char *managed_task_memory_name(ManagedTaskMemory memory);

}  // namespace lunaredu::lunaros
