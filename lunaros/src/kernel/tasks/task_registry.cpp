#include "kernel/tasks/task_registry_internal.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <type_traits>

#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#include <esp_heap_caps.h>
#include <esp_memory_utils.h>

#include "lunaredu_lunaros/kernel/log.h"
#endif

namespace lunaredu::lunaros {
namespace {

template <size_t Capacity>
bool copy_identifier(const char *source, std::array<char, Capacity> &target,
                     const char *field, std::string &error,
                     bool required = true) {
  target.fill('\0');
  if (source == nullptr || source[0] == '\0') {
    if (required) {
      error = std::string("managed_task_missing_") + field;
      return false;
    }
    return true;
  }

  const size_t length = std::strlen(source);
  if (length >= Capacity) {
    error = std::string("managed_task_") + field + "_too_long";
    return false;
  }
  std::memcpy(target.data(), source, length);
  return true;
}

bool valid_budget(const ManagedTaskBudget &budget, std::string &error) {
  if (budget.maximum_active_tasks == 0 ||
      budget.maximum_active_tasks > kManagedTaskCapacity) {
    error = "managed_task_invalid_active_budget";
    return false;
  }
  if (budget.maximum_transient_tasks > budget.maximum_active_tasks) {
    error = "managed_task_invalid_transient_budget";
    return false;
  }
  if (budget.maximum_resident_tasks == 0 ||
      budget.maximum_resident_tasks > budget.maximum_active_tasks) {
    error = "managed_task_invalid_resident_budget";
    return false;
  }
  if (budget.maximum_tasks_per_owner == 0 ||
      budget.maximum_tasks_per_owner > budget.maximum_active_tasks) {
    error = "managed_task_invalid_owner_budget";
    return false;
  }
  if (budget.maximum_internal_stack_bytes == 0 ||
      budget.maximum_external_stack_bytes == 0) {
    error = "managed_task_invalid_stack_budget";
    return false;
  }
  return true;
}

}  // namespace

ManagedTaskRegistry::ManagedTaskRegistry() {
#ifdef ESP_PLATFORM
  lock_ = xSemaphoreCreateMutexStatic(&lock_storage_);
#endif
}

bool ManagedTaskRegistry::configure(const ManagedTaskBudget &budget,
                                    std::string &error) {
  error.clear();
  if (!valid_budget(budget, error)) {
    return false;
  }

  Guard guard(lock_);
  if (!ensure_entry_storage(error)) {
    return false;
  }
  if (std::any_of(entries_, entries_ + kManagedTaskCapacity,
                  [](const Entry &entry) { return entry.active; })) {
    error = "managed_task_registry_active";
    return false;
  }
  budget_ = budget;
  configured_ = true;
  ++revision_;
  return true;
}

bool ManagedTaskRegistry::ensure_configured(std::string &error) {
  error.clear();
  Guard guard(lock_);
  if (configured_) return true;
  // Match the core runtime's build-time defaults, without reconfiguring a
  // running product or requiring a standalone device test to boot services.
  ManagedTaskBudget budget;
#ifdef CONFIG_LUNAREDU_LUNAROS_MAX_MANAGED_TASKS
  budget.maximum_active_tasks = CONFIG_LUNAREDU_LUNAROS_MAX_MANAGED_TASKS;
#endif
#ifdef CONFIG_LUNAREDU_LUNAROS_MAX_TRANSIENT_TASKS
  budget.maximum_transient_tasks = CONFIG_LUNAREDU_LUNAROS_MAX_TRANSIENT_TASKS;
#endif
#ifdef CONFIG_LUNAREDU_LUNAROS_MAX_RESIDENT_TASKS
  budget.maximum_resident_tasks = CONFIG_LUNAREDU_LUNAROS_MAX_RESIDENT_TASKS;
#endif
#ifdef CONFIG_LUNAREDU_LUNAROS_MAX_TASKS_PER_OWNER
  budget.maximum_tasks_per_owner = CONFIG_LUNAREDU_LUNAROS_MAX_TASKS_PER_OWNER;
#endif
#ifdef CONFIG_LUNAREDU_LUNAROS_MAX_INTERNAL_TASK_STACK_BYTES
  budget.maximum_internal_stack_bytes = CONFIG_LUNAREDU_LUNAROS_MAX_INTERNAL_TASK_STACK_BYTES;
#endif
#ifdef CONFIG_LUNAREDU_LUNAROS_MAX_EXTERNAL_TASK_STACK_BYTES
  budget.maximum_external_stack_bytes = CONFIG_LUNAREDU_LUNAROS_MAX_EXTERNAL_TASK_STACK_BYTES;
#endif
  if (!valid_budget(budget, error) || !ensure_entry_storage(error)) return false;
  budget_ = budget;
  configured_ = true;
  ++revision_;
  return true;
}

bool ManagedTaskRegistry::reset(std::string &error) {
  error.clear();
  Guard guard(lock_);
  if (!ensure_entry_storage(error)) {
    return false;
  }
  if (std::any_of(entries_, entries_ + kManagedTaskCapacity,
                  [](const Entry &entry) { return entry.active; })) {
    error = "managed_task_registry_active";
    return false;
  }
  std::fill(entries_, entries_ + kManagedTaskCapacity, Entry{});
  next_token_ = 1;
  ++revision_;
  return true;
}

ManagedTaskAdmission ManagedTaskRegistry::admit(
    const ManagedTaskDefinition &definition, std::string &error) {
  error.clear();
  if (definition.stack_bytes == 0) {
    error = "managed_task_invalid_stack_size";
    return {};
  }

  Entry candidate;
  if (!copy_identifier(definition.id, candidate.id, "id", error) ||
      !copy_identifier(definition.owner_service_id, candidate.owner_service_id,
                       "owner", error) ||
      !copy_identifier(definition.role, candidate.role, "role", error,
                       false)) {
    return {};
  }
  candidate.stack_bytes = definition.stack_bytes;
  candidate.priority = definition.priority;
  candidate.core_affinity = definition.core_affinity;
  candidate.lifetime = definition.lifetime;
  candidate.memory = definition.memory;
  candidate.allow_internal_fallback = definition.allow_internal_fallback;

  Guard guard(lock_);
  if (entries_ == nullptr) {
    error = "managed_task_registry_not_configured";
    return {};
  }
  size_t active = 0;
  size_t resident = 0;
  size_t transient = 0;
  size_t owner_tasks = 0;
  size_t free_slot = kManagedTaskCapacity;
  for (size_t index = 0; index < kManagedTaskCapacity; ++index) {
    const Entry &entry = entries_[index];
    if (!entry.active) {
      if (free_slot == kManagedTaskCapacity) {
        free_slot = index;
      }
      continue;
    }
    ++active;
    if (entry.lifetime == ManagedTaskLifetime::Resident) {
      ++resident;
    }
    if (entry.lifetime == ManagedTaskLifetime::Transient) {
      ++transient;
    }
    if (std::strcmp(entry.owner_service_id.data(),
                    candidate.owner_service_id.data()) == 0) {
      ++owner_tasks;
    }
    if (std::strcmp(entry.id.data(), candidate.id.data()) == 0) {
      error = "managed_task_already_active";
      return {};
    }
  }

  if (free_slot == kManagedTaskCapacity ||
      active >= budget_.maximum_active_tasks) {
    error = "managed_task_active_budget_exhausted";
    return {};
  }
  if (candidate.lifetime == ManagedTaskLifetime::Transient &&
      transient >= budget_.maximum_transient_tasks) {
    error = "managed_task_transient_budget_exhausted";
    return {};
  }
  if (candidate.lifetime == ManagedTaskLifetime::Resident &&
      resident >= budget_.maximum_resident_tasks) {
    error = "managed_task_resident_budget_exhausted";
    return {};
  }
  if (owner_tasks >= budget_.maximum_tasks_per_owner) {
    error = "managed_task_owner_budget_exhausted";
    return {};
  }
  if (!memory_budget_available_locked(candidate.memory, candidate.stack_bytes)) {
    error = candidate.memory == ManagedTaskMemory::Internal
                ? "managed_task_internal_stack_budget_exhausted"
                : "managed_task_external_stack_budget_exhausted";
    return {};
  }

  candidate.active = true;
  candidate.token = next_token_++;
  if (candidate.token == 0) {
    candidate.token = next_token_++;
  }
  entries_[free_slot] = candidate;
  ++revision_;
  return {candidate.token, free_slot};
}

bool ManagedTaskRegistry::activate(const ManagedTaskAdmission &admission,
                                   uintptr_t native_handle,
                                   std::string &error) {
  error.clear();
  if (!admission || native_handle == 0) {
    error = "managed_task_invalid_activation";
    return false;
  }
  Guard guard(lock_);
  Entry *entry = find_entry_locked(admission);
  if (entry == nullptr) {
    error = "managed_task_admission_not_found";
    return false;
  }
  entry->native_handle = native_handle;
  ++revision_;
  return true;
}

bool ManagedTaskRegistry::retarget_memory(
    const ManagedTaskAdmission &admission, ManagedTaskMemory memory,
    std::string &error) {
  error.clear();
  Guard guard(lock_);
  Entry *entry = find_entry_locked(admission);
  if (entry == nullptr) {
    error = "managed_task_admission_not_found";
    return false;
  }
  if (entry->memory == memory) {
    return true;
  }
  if (!memory_budget_available_locked(memory, entry->stack_bytes, entry)) {
    error = memory == ManagedTaskMemory::Internal
                ? "managed_task_internal_stack_budget_exhausted"
                : "managed_task_external_stack_budget_exhausted";
    return false;
  }
  entry->memory = memory;
  ++revision_;
  return true;
}

bool ManagedTaskRegistry::release(const ManagedTaskAdmission &admission) {
  if (!admission) {
    return false;
  }
  Guard guard(lock_);
  Entry *entry = find_entry_locked(admission);
  if (entry == nullptr) {
    return false;
  }
  *entry = {};
  ++revision_;
  return true;
}

ManagedTaskRegistrySnapshot ManagedTaskRegistry::snapshot() const {
  Guard guard(lock_);
  ManagedTaskRegistrySnapshot result;
  result.budget = budget_;
  result.revision = revision_;
  if (entries_ == nullptr) {
    return result;
  }
  for (size_t index = 0; index < kManagedTaskCapacity; ++index) {
    const Entry &source = entries_[index];
    if (!source.active) {
      continue;
    }
    ++result.active_tasks;
    if (source.lifetime == ManagedTaskLifetime::Resident) {
      ++result.resident_tasks;
    } else if (source.lifetime == ManagedTaskLifetime::Feature) {
      ++result.feature_tasks;
    } else if (source.lifetime == ManagedTaskLifetime::Transient) {
      ++result.transient_tasks;
    }
    if (source.memory == ManagedTaskMemory::Internal) {
      result.internal_stack_bytes += source.stack_bytes;
    } else {
      result.external_stack_bytes += source.stack_bytes;
    }

    ManagedTaskEntrySnapshot &target = result.tasks[index];
    target.active = true;
    target.token = source.token;
    target.id = source.id;
    target.owner_service_id = source.owner_service_id;
    target.role = source.role;
    target.stack_bytes = source.stack_bytes;
    target.priority = source.priority;
    target.core_affinity = source.core_affinity;
    target.lifetime = source.lifetime;
    target.memory = source.memory;
    target.allow_internal_fallback = source.allow_internal_fallback;
    target.native_handle = source.native_handle;
  }
  return result;
}

ManagedTaskRegistry::Entry *ManagedTaskRegistry::find_entry_locked(
    const ManagedTaskAdmission &admission) {
  if (entries_ == nullptr || admission.slot >= kManagedTaskCapacity) {
    return nullptr;
  }
  Entry &entry = entries_[admission.slot];
  return entry.active && entry.token == admission.token ? &entry : nullptr;
}

const ManagedTaskRegistry::Entry *ManagedTaskRegistry::find_entry_locked(
    const ManagedTaskAdmission &admission) const {
  if (entries_ == nullptr || admission.slot >= kManagedTaskCapacity) {
    return nullptr;
  }
  const Entry &entry = entries_[admission.slot];
  return entry.active && entry.token == admission.token ? &entry : nullptr;
}

bool ManagedTaskRegistry::memory_budget_available_locked(
    ManagedTaskMemory memory, size_t stack_bytes, const Entry *excluded) const {
  size_t used = 0;
  for (size_t index = 0; index < kManagedTaskCapacity; ++index) {
    const Entry &entry = entries_[index];
    if (!entry.active || &entry == excluded || entry.memory != memory) {
      continue;
    }
    used += entry.stack_bytes;
  }
  const size_t limit = memory == ManagedTaskMemory::Internal
                           ? budget_.maximum_internal_stack_bytes
                           : budget_.maximum_external_stack_bytes;
  return stack_bytes <= limit && used <= limit - stack_bytes;
}

bool ManagedTaskRegistry::ensure_entry_storage(std::string &error) {
  if (entries_ != nullptr) {
    return true;
  }
#ifdef ESP_PLATFORM
  static_assert(std::is_trivially_destructible_v<Entry>);
  void *storage = heap_caps_malloc(sizeof(Entry) * kManagedTaskCapacity,
                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (storage == nullptr || !esp_ptr_external_ram(storage)) {
    if (storage != nullptr) {
      heap_caps_free(storage);
    }
    error = "managed_task_registry_external_storage_unavailable";
    return false;
  }
  auto *entries = static_cast<Entry *>(storage);
  for (size_t index = 0; index < kManagedTaskCapacity; ++index) {
    std::construct_at(&entries[index]);
  }
  // This registry is process-lifetime state. Its slots are intentionally not
  // freed or reallocated so an admission's stable numeric slot always refers
  // to the same backing array for the whole boot.
  entries_ = entries;
  LOGI("lunaros.tasks", "registry storage=PSRAM bytes=%u",
       static_cast<unsigned>(sizeof(Entry) * kManagedTaskCapacity));
  return true;
#else
  error = "managed_task_registry_storage_unavailable";
  return false;
#endif
}

bool ManagedTaskRegistry::contains(const char *id) const {
  if (id == nullptr) return false;
  Guard guard(lock_);
  if (entries_ == nullptr) return false;
  for (size_t index = 0; index < kManagedTaskCapacity; ++index) {
    if (entries_[index].active && std::strcmp(entries_[index].id.data(), id) == 0) return true;
  }
  return false;
}

ManagedTaskRegistry &managed_task_registry_internal() {
  static ManagedTaskRegistry registry;
  return registry;
}

bool setup_managed_task_registry(const ManagedTaskBudget &budget,
                                 std::string &error) {
  return managed_task_registry_internal().configure(budget, error);
}

ManagedTaskRegistrySnapshot managed_task_registry_snapshot() {
  return managed_task_registry_internal().snapshot();
}

const char *managed_task_lifetime_name(ManagedTaskLifetime lifetime) {
  switch (lifetime) {
    case ManagedTaskLifetime::Resident:
      return "resident";
    case ManagedTaskLifetime::Feature:
      return "feature";
    case ManagedTaskLifetime::Transient:
      return "transient";
  }
  return "unknown";
}

const char *managed_task_memory_name(ManagedTaskMemory memory) {
  switch (memory) {
    case ManagedTaskMemory::Internal:
      return "internal";
    case ManagedTaskMemory::External:
      return "external";
  }
  return "unknown";
}

}  // namespace lunaredu::lunaros
