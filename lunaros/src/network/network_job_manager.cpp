#include "lunaredu_lunaros/network/network_jobs.h"

#include <algorithm>
#include <memory>
#include <type_traits>

#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#include <esp_memory_utils.h>

#include "lunaredu_lunaros/kernel/log.h"
#endif

#include "network/network_job_manager_internal.h"

namespace lunaredu::lunaros {
namespace {

NetworkJobManager g_network_job_manager;

} // namespace

bool NetworkJobManager::initialize(const NetworkJobPolicy &policy,
                                   std::string &error) {
  StaticLockGuard lock(mutex_);
  error.clear();
  if (!ensure_job_storage(error)) {
    return false;
  }
  reset_locked(policy);
  return true;
}

void NetworkJobManager::reset(const NetworkJobPolicy &policy) {
  StaticLockGuard lock(mutex_);
  reset_locked(policy);
}

void NetworkJobManager::reset_locked(const NetworkJobPolicy &policy) {
  if (jobs_ != nullptr) {
    std::fill(jobs_, jobs_ + kNetworkJobCapacity, JobRecord{});
  }
  policy_ = internal::normalize_network_job_policy(policy);
  metrics_ = {};
  // Handles retained by clients must never alias jobs submitted after reset.
  next_sequence_ = 1;
  maintenance_state_ = NetworkMaintenanceState::Normal;
  wake_handler_ = nullptr;
  wake_context_ = nullptr;
}

void NetworkJobManager::set_wake_handler(NetworkJobWakeHandler handler,
                                         void *context) {
  StaticLockGuard lock(mutex_);
  wake_handler_ = handler;
  wake_context_ = context;
}

bool NetworkJobManager::submit(const NetworkJobDefinition &definition,
                               NetworkJobHandle &handle,
                               std::string &error) {
  NetworkJobWakeHandler wake_handler = nullptr;
  void *wake_context = nullptr;
  {
    StaticLockGuard lock(mutex_);
    error.clear();
    handle = {};
    if (jobs_ == nullptr) {
      error = "network_job_manager_not_initialized";
      return false;
    }
    if (!internal::valid_network_job_definition(definition)) {
      ++metrics_.rejected_invalid;
      error = "invalid_network_job_definition";
      return false;
    }
    const bool ota_job = definition.job_class == NetworkJobClass::Ota;
    const bool maintenance_allows_job =
        ota_job ? maintenance_state_ != NetworkMaintenanceState::Normal
                : maintenance_state_ == NetworkMaintenanceState::Normal;
    if (!maintenance_allows_job) {
      ++metrics_.rejected_maintenance;
      error = ota_job ? "network_ota_maintenance_required"
                      : "network_ota_maintenance_active";
      return false;
    }
    size_t free_slots = 0;
    for (size_t index = 0; index < kNetworkJobCapacity; ++index) {
      if (jobs_[index].snapshot.state == NetworkJobState::Free) {
        ++free_slots;
      }
    }
    if (!ota_job && free_slots <= kNetworkOtaReservedSlots) {
      ++metrics_.rejected_capacity;
      error = "network_job_ota_reserve";
      return false;
    }
    // The registry has four physical slots beyond the normal active-job
    // policy. OTA uses that reserve so a full background queue cannot prevent
    // the maintenance control job or firmware pull from being admitted.
    if (!ota_job &&
        (policy_.maximum_active_jobs == 0 ||
         active_count_locked() >= policy_.maximum_active_jobs)) {
      ++metrics_.rejected_capacity;
      error = "network_job_queue_full";
      return false;
    }
    if (!ota_job &&
        (policy_.maximum_jobs_per_owner == 0 ||
        owner_active_count_locked(definition.owner_service_id) >=
            policy_.maximum_jobs_per_owner)) {
      ++metrics_.rejected_owner_limit;
      error = "network_job_owner_limit";
      return false;
    }

    JobRecord *target = nullptr;
    for (size_t index = 0; index < kNetworkJobCapacity; ++index) {
      JobRecord &record = jobs_[index];
      if (record.snapshot.state == NetworkJobState::Free) {
        target = &record;
        break;
      }
    }
    if (target == nullptr) {
      ++metrics_.rejected_capacity;
      error = "network_job_registry_full";
      return false;
    }

    if (next_id_ == 0) {
      error = "network_job_ids_exhausted";
      return false;
    }
    const uint32_t id = next_id_++;
    target->snapshot.handle = {id};
    target->snapshot.state = NetworkJobState::Queued;
    target->snapshot.owner_service_id = definition.owner_service_id;
    target->snapshot.kind = definition.kind;
    target->snapshot.job_class = definition.job_class;
    target->snapshot.priority = definition.priority;
    target->snapshot.maximum_attempts = definition.maximum_attempts;
    target->snapshot.deadline_ms = definition.deadline_ms;
    target->snapshot.sequence = next_sequence_++;
    target->runner = definition.runner;
    target->context = definition.context;
    target->retain_terminal = definition.retain_terminal;
    target->retry_backoff_initial_ms =
        definition.retry_backoff_initial_ms == 0
            ? policy_.default_retry_backoff_ms
            : definition.retry_backoff_initial_ms;
    target->retry_backoff_maximum_ms =
        definition.retry_backoff_maximum_ms == 0
            ? policy_.maximum_retry_backoff_ms
            : definition.retry_backoff_maximum_ms;
    target->retry_backoff_maximum_ms = std::max(
        target->retry_backoff_initial_ms, target->retry_backoff_maximum_ms);
    handle = target->snapshot.handle;
    ++metrics_.submitted;
    wake_handler = wake_handler_;
    wake_context = wake_context_;
  }
  // Never invoke external code while the scheduler mutex is held.
  if (wake_handler != nullptr) {
    wake_handler(wake_context);
  }
  return true;
}

bool NetworkJobManager::ensure_job_storage(std::string &error) {
  if (jobs_ != nullptr) {
    return true;
  }
#ifdef ESP_PLATFORM
  static_assert(std::is_trivially_destructible_v<JobRecord> == false);
  void *storage = heap_caps_malloc(sizeof(JobRecord) * kNetworkJobCapacity,
                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (storage == nullptr || !esp_ptr_external_ram(storage)) {
    if (storage != nullptr) {
      heap_caps_free(storage);
    }
    error = "network_job_manager_external_storage_unavailable";
    return false;
  }
  auto *jobs = static_cast<JobRecord *>(storage);
  for (size_t index = 0; index < kNetworkJobCapacity; ++index) {
    std::construct_at(&jobs[index]);
  }
  // Runner callbacks may be active while service shutdown is requested. Keep
  // this non-trivial slot array alive and at one address for the whole boot.
  jobs_ = jobs;
  LOGI("lunaros.netjobs", "registry storage=PSRAM bytes=%u",
       static_cast<unsigned>(sizeof(JobRecord) * kNetworkJobCapacity));
  return true;
#else
  error = "network_job_manager_storage_unavailable";
  return false;
#endif
}

NetworkJobManager &network_job_manager_internal() { return g_network_job_manager; }

bool setup_network_job_manager_internal(const NetworkJobPolicy &policy,
                                        std::string &error) {
  return g_network_job_manager.initialize(policy, error);
}

void reset_network_job_manager_internal(const NetworkJobPolicy &policy) {
  g_network_job_manager.reset(policy);
}

bool submit_network_job(const NetworkJobDefinition &definition,
                        NetworkJobHandle &handle, std::string &error) {
  return g_network_job_manager.submit(definition, handle, error);
}

void set_network_job_wake_handler(NetworkJobWakeHandler handler,
                                  void *context) {
  g_network_job_manager.set_wake_handler(handler, context);
}

bool cancel_network_job(NetworkJobHandle handle, std::string &error) {
  return g_network_job_manager.cancel(handle, error);
}

bool network_job_cancellation_requested(NetworkJobHandle handle) {
  return g_network_job_manager.cancellation_requested(handle);
}

bool network_job_active(NetworkJobHandle handle) {
  return g_network_job_manager.active(handle);
}

bool begin_network_ota_maintenance(std::string &error) {
  return g_network_job_manager.begin_ota_maintenance(error);
}

bool activate_network_ota_maintenance(std::string &error) {
  return g_network_job_manager.activate_ota_maintenance(error);
}

void end_network_ota_maintenance() {
  g_network_job_manager.end_ota_maintenance();
}

NetworkMaintenanceState network_maintenance_state() {
  return g_network_job_manager.maintenance_state();
}

bool run_next_network_job(uint32_t now_ms) {
  return g_network_job_manager.run_next(now_ms);
}

uint32_t next_network_job_wake_delay_ms(uint32_t now_ms) {
  return g_network_job_manager.next_wake_delay_ms(now_ms);
}

bool release_network_job(NetworkJobHandle handle, std::string &error) {
  return g_network_job_manager.release(handle, error);
}

NetworkJobManagerSnapshot network_job_manager_snapshot() {
  return g_network_job_manager.snapshot();
}

} // namespace lunaredu::lunaros
