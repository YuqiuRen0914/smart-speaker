#include "kernel/tasks/task_registry_internal.h"
#include "platform/system.h"

#include <cstring>

#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#include <esp_heap_caps.h>
#include <esp_memory_utils.h>
#include <freertos/task.h>

#endif

namespace lunaredu::lunaros {
namespace {
uint32_t task_hash(const char *id) {
  uint32_t hash=2166136261U;
  for(const auto *p=reinterpret_cast<const unsigned char *>(id);*p;++p)
    hash=(hash^*p)*16777619U;
  return hash;
}

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

template <typename Entry, typename Observation>
void copy_metadata(const Entry &source, Observation &target) {
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
  target.owner_kind = source.owner_kind;
}

// Both observations account for the same live admission, under the registry
// lock. The full snapshot additionally measures native stacks below.
template <typename Entry, typename Observation>
void account_reservation(const Entry &entry, Observation &result) {
  ++result.active_tasks;
  if (entry.lifetime == ManagedTaskLifetime::Resident) {
    ++result.resident_tasks;
  } else if (entry.lifetime == ManagedTaskLifetime::Feature) {
    ++result.feature_tasks;
  } else if (entry.lifetime == ManagedTaskLifetime::Transient) {
    ++result.transient_tasks;
  }
  if (entry.memory != ManagedTaskMemory::External) {
    result.internal_stack_bytes += entry.stack_bytes;
  } else {
    result.external_stack_bytes += entry.stack_bytes;
  }
}

}  // namespace

ManagedTaskRegistry::ManagedTaskRegistry() = default;
ExecutionLease ManagedTaskRegistry::execution_lease(const char *id) const {
  StaticLockGuard guard(lock_);
  const auto *entry=find_active_entry_locked(id);
  return entry?entry->execution_lease:ExecutionLease{};
}

ManagedTaskRegistry::~ManagedTaskRegistry() = default;

bool ManagedTaskRegistry::ensure_configured(std::string &error) {
  error.clear();
  StaticLockGuard guard(lock_);
  if (!configured_) {
    configured_ = true;
    ++revision_;
  }
  return true;
}

bool ManagedTaskRegistry::can_reset(std::string &error) const {
  error.clear();
  StaticLockGuard guard(lock_);
  for (size_t index = 0; index < entries_.size(); ++index) {
    if (entries_[index].active) {
      error = "managed_task_registry_active";
      return false;
    }
  }
  return true;
}

bool ManagedTaskRegistry::reset(std::string &error) {
  error.clear();
  StaticLockGuard guard(lock_);
  for (size_t index = 0; index < entries_.size(); ++index) {
    if (entries_[index].active) {
      error = "managed_task_registry_active";
      return false;
    }
  }
  entries_.clear();
  lookup_.clear();
  owners_.clear();
  // Keep admission identities across reset: a delayed request from a retired
  // task must not address a replacement that reuses its registry slot.
  ++revision_;
  return true;
}

ManagedTaskAdmission ManagedTaskRegistry::admit(
    const ManagedTaskDefinition &definition, std::string &error) {
  error.clear();
  const bool migrating=definition.allow_core_migration&&
      definition.core_affinity==kExecutionAnyCore&&definition.execution.kind!=ExecutionClass::Deadline;
  if ((!valid_managed_task_core(definition.core_affinity)||definition.allow_core_migration)&&!migrating) {
    error = "managed_task_invalid_core_affinity";
    return {};
  }
  if (static_cast<unsigned>(definition.lifetime) > static_cast<unsigned>(ManagedTaskLifetime::Transient) ||
      static_cast<unsigned>(definition.memory) > static_cast<unsigned>(ManagedTaskMemory::InternalDma) ||
      static_cast<unsigned>(definition.owner_kind) > static_cast<unsigned>(ManagedTaskOwnerKind::Subsystem)) {
    error = "managed_task_invalid_definition";
    return {};
  }
  if (definition.stack_bytes == 0) {
    error = "managed_task_invalid_stack_size";
    return {};
  }
#if defined(ESP_PLATFORM) && defined(configMAX_PRIORITIES)
  if(definition.priority>=configMAX_PRIORITIES) {
    error="managed_task_invalid_priority";return {};
  }
#endif

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
  candidate.owner_kind = definition.owner_kind;

  StaticLockGuard guard(lock_);
  if (!configured_) {
    error = "managed_task_registry_not_configured";
    return {};
  }
  if (find_active_entry_locked(candidate.id.data())) {
    error = "managed_task_already_active";
    return {};
  }
  if (!next_token_) {
    error = "managed_task_identity_exhausted";
    return {};
  }
  const auto previous_count=lookup_.size();
  if(!lookup_.resize(previous_count+1)) {
    error="managed_task_registry_storage_unavailable";return {};
  }
  const auto restore_lookup=[&]{
    if(previous_count)(void)lookup_.resize(previous_count);else lookup_.clear();
  };
  Entry *record = entries_.append();
  if (!record) {
    restore_lookup();
    error = "managed_task_registry_storage_unavailable";
    return {};
  }

  auto contract = definition.execution;
  contract.priority = definition.priority;
  contract.core = definition.core_affinity;
  const char *execution_error = nullptr;
  if (!execution_.reserve(candidate.id.data(), contract,
                          candidate.execution_lease, execution_error,true)) {
    entries_.erase(entries_.size() - 1);
    restore_lookup();
    error = execution_error ? execution_error : "managed_task_execution_rejected";
    return {};
  }
  candidate.active = true;
  candidate.token = next_token_++;
  *record = candidate;
  const auto hash=task_hash(record->id.data());
  size_t slot=previous_count;
  while(slot&&lookup_[slot-1].hash>hash){lookup_[slot]=lookup_[slot-1];--slot;}
  lookup_[slot]={hash,record};
  ++revision_;
  return {candidate.token, reinterpret_cast<uintptr_t>(record)};
}

bool ManagedTaskRegistry::activate(const ManagedTaskAdmission &admission,
                                   uintptr_t native_handle,
                                   std::string &error) {
  error.clear();
  if (!admission || native_handle == 0) {
    error = "managed_task_invalid_activation";
    return false;
  }
  StaticLockGuard guard(lock_);
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
  if (static_cast<unsigned>(memory) > static_cast<unsigned>(ManagedTaskMemory::InternalDma)) {
    error = "managed_task_invalid_memory";
    return false;
  }
  StaticLockGuard guard(lock_);
  Entry *entry = find_entry_locked(admission);
  if (entry == nullptr) {
    error = "managed_task_admission_not_found";
    return false;
  }
  if (entry->memory == memory) {
    return true;
  }
  entry->memory = memory;
  ++revision_;
  return true;
}

bool ManagedTaskRegistry::release(const ManagedTaskAdmission &admission) {
  if (!admission) {
    return false;
  }
  StaticLockGuard guard(lock_);
  Entry *entry = find_entry_locked(admission);
  if (entry == nullptr) {
    return false;
  }
  if (!execution_.release(entry->id.data(), entry->execution_lease)) return false;
  for(size_t i=0;i<lookup_.size();++i)if(lookup_[i].entry==entry){
    for(size_t j=i+1;j<lookup_.size();++j)lookup_[j-1]=lookup_[j];
    if(lookup_.size()==1)lookup_.clear();else (void)lookup_.resize(lookup_.size()-1);
    break;
  }
  for (size_t index = 0; index < entries_.size(); ++index) {
    if (&entries_[index] == entry) { entries_.erase(index); break; }
  }
  ++revision_;
  return true;
}

ManagedTaskSummary ManagedTaskRegistry::summary() const {
  StaticLockGuard guard(lock_);
  ManagedTaskSummary result;
  result.revision = revision_;
  result.storage = storage_snapshot_locked();
  for (size_t index = 0; index < entries_.size(); ++index)
    if (entries_[index].active) account_reservation(entries_[index], result);
  return result;
}

ManagedTaskRegistrySnapshot ManagedTaskRegistry::snapshot() const {
  const auto before=system_monotonic_us();
  StaticLockGuard guard(lock_);
  const auto acquired=system_monotonic_us();
  ManagedTaskRegistrySnapshot result;
  const auto complete=[&] {
    result.read_cost=read_timing_.record(acquired-before,system_monotonic_us()-acquired);
  };
  result.revision = revision_;
  result.storage = storage_snapshot_locked();
  for (size_t index = 0; index < entries_.size(); ++index)
    if (entries_[index].active) account_reservation(entries_[index], result);
  if (!result.tasks.resize(result.active_tasks)) {
    result.complete = false;
    complete(); return result;
  }
  size_t captured = 0;
  for (size_t index = 0; index < entries_.size(); ++index) {
    const Entry &source = entries_[index];
    if (!source.active) {
      continue;
    }
    ManagedTaskEntrySnapshot &target = result.tasks[captured++];
    copy_metadata(source, target);
#ifdef ESP_PLATFORM
    if (source.native_handle != 0) {
      target.minimum_free_stack_bytes = uxTaskGetStackHighWaterMark(
          reinterpret_cast<TaskHandle_t>(source.native_handle));
    }
#endif
  }
  complete(); return result;
}

ManagedTaskMetadataSnapshot ManagedTaskRegistry::metadata() const {
  StaticLockGuard guard(lock_);
  ManagedTaskMetadataSnapshot result;
  result.revision = revision_;
  for (size_t index = 0; index < entries_.size(); ++index)
    result.observed_tasks += entries_[index].active;
  if (!result.tasks.resize(result.observed_tasks)) {
    result.complete = false;
    return result;
  }
  size_t captured = 0;
  for (size_t index = 0; index < entries_.size(); ++index)
    if (entries_[index].active) copy_metadata(entries_[index], result.tasks[captured++]);
  return result;
}

bool ManagedTaskRegistry::declare_owner(ManagedTaskOwnerKind kind, const char *id, std::string &error) {
  if (static_cast<unsigned>(kind) > static_cast<unsigned>(ManagedTaskOwnerKind::Subsystem)) {
    error = "managed_task_invalid_owner_kind";
    return false;
  }
  Owner owner; owner.kind = kind;
  if (!copy_identifier(id, owner.id, "owner", error)) return false;
  StaticLockGuard guard(lock_);
  for (size_t index = 0; index < owners_.size(); ++index) {
    auto &entry = owners_[index];
    if (entry.kind == kind && !std::strcmp(entry.id.data(), id)) { error.clear(); return true; }
  }
  Owner *available = owners_.append();
  if (!available) { error = "managed_task_owner_storage_unavailable"; return false; }
  *available = owner; error.clear(); return true;
}
bool ManagedTaskRegistry::forget_owner(ManagedTaskOwnerKind kind, const char *id) {
  if (!id) return false;
  StaticLockGuard guard(lock_);
  for (size_t index = 0; index < entries_.size(); ++index)
    if (entries_[index].active && entries_[index].owner_kind == kind &&
        !std::strcmp(entries_[index].owner_service_id.data(), id)) return false;
  for (size_t index = 0; index < owners_.size(); ++index) {
    auto &entry = owners_[index];
    if (entry.kind == kind && !std::strcmp(entry.id.data(), id)) { owners_.erase(index); return true; }
  }
  return false;
}
bool ManagedTaskRegistry::owner_registered(ManagedTaskOwnerKind kind, const char *id) const {
  if (!id || !*id) return false;
  StaticLockGuard guard(lock_);
  for (size_t index = 0; index < owners_.size(); ++index) {
    const auto &entry = owners_[index];
    if (entry.kind == kind && !std::strcmp(entry.id.data(), id)) return true;
  }
  return false;
}

ManagedTaskRegistry::Entry *ManagedTaskRegistry::find_entry_locked(
    const ManagedTaskAdmission &admission) {
  for (size_t index = 0; index < entries_.size(); ++index) {
    Entry &entry = entries_[index];
    if (reinterpret_cast<uintptr_t>(&entry) == admission.identity && entry.token == admission.token)
      return &entry;
  }
  return nullptr;
}

const ManagedTaskRegistry::Entry *ManagedTaskRegistry::find_entry_locked(
    const ManagedTaskAdmission &admission) const {
  for (size_t index = 0; index < entries_.size(); ++index) {
    const Entry &entry = entries_[index];
    if (reinterpret_cast<uintptr_t>(&entry) == admission.identity && entry.token == admission.token)
      return &entry;
  }
  return nullptr;
}

ManagedTaskStorageSnapshot ManagedTaskRegistry::storage_snapshot_locked() const {
  ManagedTaskStorageSnapshot result;
  result.capacity = entries_.size();
  result.storage_bytes = entries_.storage_bytes() + owners_.storage_bytes() + lookup_.capacity()*sizeof(Lookup);
  if (!result.storage_bytes) return result;
#ifdef ESP_PLATFORM
  const bool external = entries_.size() ? esp_ptr_external_ram(&entries_[0]) : esp_ptr_external_ram(&owners_[0]);
  const bool mixed = entries_.size() && owners_.size() &&
      external != esp_ptr_external_ram(&owners_[0]);
  result.memory = mixed ? "mixed" : external ? "psram" : "internal";
#else
  result.memory = "host";
#endif
  return result;
}

const ManagedTaskRegistry::Entry *
ManagedTaskRegistry::find_active_entry_locked(const char *id) const {
  if (id == nullptr || id[0] == '\0') return nullptr;
  const auto hash=task_hash(id);
  size_t first=0,last=lookup_.size();
  while(first<last){const auto middle=first+(last-first)/2;
    if(lookup_[middle].hash<hash)first=middle+1;else last=middle;
  }
  for(size_t i=first;i<lookup_.size()&&lookup_[i].hash==hash;++i){
    const auto *entry=lookup_[i].entry;
    if(entry->active&&std::strcmp(entry->id.data(),id)==0)return entry;
  }
  return nullptr;
}

bool ManagedTaskRegistry::contains(const char *id) const {
  StaticLockGuard guard(lock_);
  return find_active_entry_locked(id) != nullptr;
}

bool ManagedTaskRegistry::running(const char *id) const {
  StaticLockGuard guard(lock_);
  const Entry *entry = find_active_entry_locked(id);
  return entry != nullptr && entry->native_handle != 0;
}

bool ManagedTaskRegistry::contains(const char *id, ManagedTaskOwnerKind kind,
                                   const char *owner_id) const {
  if (owner_id == nullptr || owner_id[0] == '\0') return false;
  StaticLockGuard guard(lock_);
  const Entry *entry = find_active_entry_locked(id);
  return entry != nullptr && entry->owner_kind == kind &&
         std::strcmp(entry->owner_service_id.data(), owner_id) == 0;
}

bool ManagedTaskRegistry::running(const char *id, ManagedTaskOwnerKind kind,
                                  const char *owner_id) const {
  if (owner_id == nullptr || owner_id[0] == '\0') return false;
  StaticLockGuard guard(lock_);
  const Entry *entry = find_active_entry_locked(id);
  return entry != nullptr && entry->native_handle != 0 &&
         entry->owner_kind == kind &&
         std::strcmp(entry->owner_service_id.data(), owner_id) == 0;
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
    case ManagedTaskMemory::InternalDma:
      return "internal_dma";
  }
  return "unknown";
}

}  // namespace lunaredu::lunaros
