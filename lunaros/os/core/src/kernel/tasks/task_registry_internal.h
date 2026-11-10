#pragma once

#include <array>
#include "kernel/managed_task.h"
#include "kernel/static_mutex.h"

namespace lunaredu::lunaros {

struct ManagedTaskAdmission {
  uint64_t token = 0;
  uintptr_t identity = 0;

  explicit operator bool() const { return token != 0; }
};


class ManagedTaskRegistry {
public:
  ManagedTaskRegistry();
  ~ManagedTaskRegistry();
  ManagedTaskRegistry(const ManagedTaskRegistry &) = delete;
  ManagedTaskRegistry &operator=(const ManagedTaskRegistry &) = delete;
  bool ensure_configured(std::string &error);
  bool can_reset(std::string &error) const;
  bool reset(std::string &error);
  ManagedTaskAdmission admit(const ManagedTaskDefinition &definition,
                             std::string &error);
  bool activate(const ManagedTaskAdmission &admission, uintptr_t native_handle,
                std::string &error);
  bool retarget_memory(const ManagedTaskAdmission &admission,
                       ManagedTaskMemory memory, std::string &error);
  bool release(const ManagedTaskAdmission &admission);
  ManagedTaskRegistrySnapshot snapshot() const;
  ManagedTaskSummary summary() const;
  ManagedTaskMetadataSnapshot metadata() const;
  bool contains(const char *id) const;
  bool running(const char *id) const;
  bool contains(const char *id, ManagedTaskOwnerKind, const char *owner_id) const;
  bool running(const char *id, ManagedTaskOwnerKind, const char *owner_id) const;
  bool declare_owner(ManagedTaskOwnerKind, const char *, std::string &);
  bool forget_owner(ManagedTaskOwnerKind, const char *);
  bool owner_registered(ManagedTaskOwnerKind, const char *) const;
  ExecutionAdmission &execution() { return execution_; }
  const ExecutionAdmission &execution() const { return execution_; }
  ExecutionLease execution_lease(const char *id) const;

private:
  struct Entry {
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
    ExecutionLease execution_lease;
  };

  Entry *find_entry_locked(const ManagedTaskAdmission &admission);
  const Entry *find_entry_locked(const ManagedTaskAdmission &admission) const;
  const Entry *find_active_entry_locked(const char *id) const;
  ManagedTaskStorageSnapshot storage_snapshot_locked() const;

  mutable StaticMutex lock_;
  mutable system::DiagnosticReadTiming read_timing_;
  ExecutionAdmission execution_;
  bool configured_ = false;
  RegistrationTable<Entry> entries_{true};
  // Stable record pointers; hot identity checks touch a compact index rather
  // than every cold task declaration in external memory.
  struct Lookup { uint32_t hash=0; Entry *entry=nullptr; };
  RegistrationArray<Lookup> lookup_{true};
  uint64_t next_token_ = 1;
  uint32_t revision_ = 0;
  struct Owner { std::array<char, kManagedTaskOwnerBytes> id{}; ManagedTaskOwnerKind kind = ManagedTaskOwnerKind::Service; };
  RegistrationTable<Owner> owners_{true};
};

} // namespace lunaredu::lunaros
