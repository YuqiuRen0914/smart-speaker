#pragma once

#include <array>
#include <atomic>

#ifdef ESP_PLATFORM
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#endif

#include "lunaredu_lunaros/kernel/task_registry.h"

namespace lunaredu::lunaros {

class ManagedTaskRegistry {
public:
  ManagedTaskRegistry();
  bool configure(const ManagedTaskBudget &budget, std::string &error);
  bool ensure_configured(std::string &error);
  bool reset(std::string &error);
  ManagedTaskAdmission admit(const ManagedTaskDefinition &definition,
                             std::string &error);
  bool activate(const ManagedTaskAdmission &admission, uintptr_t native_handle,
                std::string &error);
  bool retarget_memory(const ManagedTaskAdmission &admission,
                       ManagedTaskMemory memory, std::string &error);
  bool release(const ManagedTaskAdmission &admission);
  ManagedTaskRegistrySnapshot snapshot() const;
  bool contains(const char *id) const;

private:
  class Guard {
  public:
#ifdef ESP_PLATFORM
    explicit Guard(SemaphoreHandle_t lock) : lock_(lock) {
      if (lock_ != nullptr) {
        (void)xSemaphoreTake(lock_, portMAX_DELAY);
      }
    }
    ~Guard() {
      if (lock_ != nullptr) {
        (void)xSemaphoreGive(lock_);
      }
    }
#else
    explicit Guard(std::atomic_flag &lock) : lock_(lock) {
      while (lock_.test_and_set(std::memory_order_acquire)) {
      }
    }
    ~Guard() { lock_.clear(std::memory_order_release); }
#endif
    Guard(const Guard &) = delete;
    Guard &operator=(const Guard &) = delete;

  private:
#ifdef ESP_PLATFORM
    SemaphoreHandle_t lock_ = nullptr;
#else
    std::atomic_flag &lock_;
#endif
  };

  struct Entry {
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

  Entry *find_entry_locked(const ManagedTaskAdmission &admission);
  const Entry *find_entry_locked(const ManagedTaskAdmission &admission) const;
  bool memory_budget_available_locked(ManagedTaskMemory memory,
                                      size_t stack_bytes,
                                      const Entry *excluded = nullptr) const;
  bool ensure_entry_storage(std::string &error);

#ifdef ESP_PLATFORM
  mutable StaticSemaphore_t lock_storage_{};
  mutable SemaphoreHandle_t lock_ = nullptr;
#else
  mutable std::atomic_flag lock_ = ATOMIC_FLAG_INIT;
#endif
  ManagedTaskBudget budget_{};
  bool configured_ = false;
#ifdef ESP_PLATFORM
  Entry *entries_ = nullptr;
#else
  std::array<Entry, kManagedTaskCapacity> owned_entries_{};
  Entry *entries_ = owned_entries_.data();
#endif
  uint32_t next_token_ = 1;
  uint32_t revision_ = 0;
};

ManagedTaskRegistry &managed_task_registry_internal();

} // namespace lunaredu::lunaros
