#include "lunaros_network_lifecycle.h"
#include "lunaros_network_jobs.h"

#include "network/network_job_manager_internal.h"
#include <chrono>
#include <algorithm>
#include <cstring>
#ifdef ESP_PLATFORM
#include "diagnostics/cpu_monitor.h"
#include "diagnostics/current_task_runtime.h"
#include <esp_log.h>
#endif

namespace lunaredu::lunaros {
namespace {

class RunnerContextGuard;
// One pointer replaces the previous depth counter, with no extra per-task TLS.
// The frame and previous frame live on the existing runner's stack.
thread_local const RunnerContextGuard *g_runner_context = nullptr;

uint32_t monotonic_ms() {
  return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count());
}

class RunnerContextGuard {
public:
  RunnerContextGuard(NetworkJobManager &manager, NetworkJobHandle handle)
      : manager_(manager), handle_(handle), previous_(g_runner_context) {
    g_runner_context = this;
  }
  ~RunnerContextGuard() { g_runner_context = previous_; }
  bool should_yield() const { return manager_.should_yield_transport_setup(handle_); }
  bool cancellation_requested() const {
    return manager_.cancellation_requested(handle_)||(previous_&&previous_->cancellation_requested());
  }
  uint32_t remaining_timeout_ms(uint32_t ceiling) const {
    const auto own=manager_.remaining_timeout_ms(handle_,ceiling);
    return previous_?previous_->remaining_timeout_ms(own):own;
  }
  bool accepts_normal_work() const { return manager_.accepts_nested_normal_work(handle_); }
  void set_phase(NetworkJobPhase phase) const {
    if (phase > NetworkJobPhase::ResourceCleanup || phase == phase_) return;
    phase_ = phase;
    manager_.set_phase(handle_, phase);
  }
private:
  NetworkJobManager &manager_;
  NetworkJobHandle handle_;
  const RunnerContextGuard *previous_;
  mutable NetworkJobPhase phase_ = NetworkJobPhase::Unspecified;
};

} // namespace

bool NetworkJobManager::cancel(NetworkJobHandle handle, std::string &error) {
  NetworkJobWakeHandler wake_handler = nullptr;
  void *wake_context = nullptr;
  {
    StaticLockGuard lock(mutex_);
    error.clear();
    const int index = find_locked(handle);
    if (index < 0) {
      error = "network_job_not_found";
      return false;
    }
    JobRecord &record = jobs_[static_cast<size_t>(index)];
    NetworkJobSnapshot &job = record.snapshot;
    if (internal::network_job_terminal(job.state)) {
      error = "network_job_already_finished";
      return false;
    }
    job.cancellation_requested = true;
    if (job.state == NetworkJobState::Queued) {
      job.state = NetworkJobState::Cancelled;
      job.detail = "cancelled_before_start";
      --active_count_;
      ++metrics_.cancelled;
      ++metrics_.completed;
      if (!record.retain_terminal) {
        record = {};
        --occupied_count_;
      }
      wake_handler = wake_handler_;
      wake_context = wake_context_;
    }
  }
  if (wake_handler != nullptr) {
    wake_handler(wake_context);
  }
  return true;
}

bool NetworkJobManager::cancellation_requested(NetworkJobHandle handle) const {
  StaticLockGuard lock(mutex_);
  const int index = find_locked(handle);
  return index >= 0 &&
         jobs_[static_cast<size_t>(index)].snapshot.cancellation_requested;
}

bool NetworkJobManager::active(NetworkJobHandle handle) const {
  StaticLockGuard lock(mutex_);
  const int index = find_locked(handle);
  if (index < 0) return false;
  const auto state = jobs_[static_cast<size_t>(index)].snapshot.state;
  return state == NetworkJobState::Queued || state == NetworkJobState::Running;
}

uint32_t NetworkJobManager::remaining_timeout_ms(NetworkJobHandle handle,uint32_t ceiling_ms) const {
  StaticLockGuard lock(mutex_);
  const int index=find_locked(handle);
  if(index<0)return 0;
  const auto deadline=jobs_[static_cast<size_t>(index)].snapshot.deadline_ms;
  if(!deadline)return ceiling_ms;
  const auto now=read_clock_ms();
  if(internal::network_job_time_reached(now,deadline))return 0;
  return std::min(ceiling_ms,internal::network_job_time_until(now,deadline));
}

bool NetworkJobManager::should_yield_transport_setup(NetworkJobHandle handle) const {
  StaticLockGuard lock(mutex_);
  const int index = find_locked(handle);
  if (index < 0) return false;
  const auto &job = jobs_[static_cast<size_t>(index)].snapshot;
  return job.state == NetworkJobState::Running &&
      job.job_class == NetworkJobClass::Normal && job.yieldable_transport_setup &&
      (retirement_window_count_ != 0 || maintenance_state_ != NetworkMaintenanceState::Normal ||
       job.cancellation_requested);
}

bool NetworkJobManager::accepts_nested_normal_work(NetworkJobHandle handle) const {
  StaticLockGuard lock(mutex_);
  const int index = find_locked(handle);
  if (index < 0 || admission_closed_ || retirement_window_count_ != 0 ||
      maintenance_state_ != NetworkMaintenanceState::Normal) return false;
  const auto &job = jobs_[static_cast<size_t>(index)].snapshot;
  return job.state == NetworkJobState::Running &&
      job.job_class == NetworkJobClass::Normal && !job.cancellation_requested;
}

void NetworkJobManager::set_phase(NetworkJobHandle handle, NetworkJobPhase phase) {
  if (phase > NetworkJobPhase::ResourceCleanup) return;
  StaticLockGuard lock(mutex_);
  const int index = find_locked(handle);
  if (index >= 0 && jobs_[static_cast<size_t>(index)].snapshot.state == NetworkJobState::Running)
    jobs_[static_cast<size_t>(index)].snapshot.phase = phase;
}

bool NetworkJobManager::acquire_retirement_window(NetworkRetirementWindow &window,
                                                  std::string &error) {
  StaticLockGuard lock(mutex_);
  error.clear();
  if (jobs_ == nullptr) { error = "network_job_manager_not_initialized"; return false; }
  if (window) {
    for (uint32_t id : retirement_windows_) if (id == window.id) return true;
    error = "network_retirement_window_not_found"; return false;
  }
  if (next_retirement_window_id_ == 0) {
    error = "network_retirement_window_ids_exhausted"; return false;
  }
  for (auto &id : retirement_windows_) {
    if (id != 0) continue;
    id = next_retirement_window_id_++;
    window.id = id;
    ++retirement_window_count_;
    return true;
  }
  error = "network_retirement_window_capacity";
  return false;
}

bool NetworkJobManager::release_retirement_window(NetworkRetirementWindow &window,
                                                  std::string &error) {
  NetworkJobWakeHandler wake_handler = nullptr;
  void *wake_context = nullptr;
  {
    StaticLockGuard lock(mutex_);
    error.clear();
    if (!window) return true;
    auto found = std::find(retirement_windows_.begin(), retirement_windows_.end(), window.id);
    if (found == retirement_windows_.end()) {
      error = "network_retirement_window_not_found"; return false;
    }
    *found = 0;
    window = {};
    if (--retirement_window_count_ == 0) {
      wake_handler = wake_handler_; wake_context = wake_context_;
    }
  }
  if (wake_handler != nullptr) wake_handler(wake_context);
  return true;
}

bool NetworkJobManager::acquire_external_work(NetworkExternalWork &work, std::string &error) {
  StaticLockGuard lock(mutex_);
  error.clear();
  if (work) {
    if (work.id == external_work_id_) return true;
    error = "network_external_work_stale"; return false;
  }
  if (!jobs_ || admission_closed_ || retirement_window_count_ != 0 ||
      maintenance_state_ != NetworkMaintenanceState::Normal) {
    error = "network_external_work_admission_closed"; return false;
  }
  if (external_work_id_ != 0 || next_external_work_id_ == 0) {
    error = "network_external_work_unavailable"; return false;
  }
  external_work_id_ = next_external_work_id_++;
  work.id = external_work_id_;
  return true;
}

bool NetworkJobManager::release_external_work(NetworkExternalWork &work, std::string &error) {
  NetworkJobWakeHandler wake = nullptr;
  void *context = nullptr;
  {
    StaticLockGuard lock(mutex_);
    error.clear();
    if (!work) return true;
    if (work.id != external_work_id_) { error = "network_external_work_stale"; return false; }
    external_work_id_ = 0;
    work = {};
    wake = wake_handler_; context = wake_context_;
  }
  if (wake) wake(context);
  return true;
}

bool NetworkJobManager::begin_ota_maintenance(std::string &error) {
  NetworkJobWakeHandler wake_handler = nullptr;
  void *wake_context = nullptr;
  {
    StaticLockGuard lock(mutex_);
    error.clear();
    if (maintenance_state_ != NetworkMaintenanceState::Normal) {
      return true;
    }
    maintenance_state_ = NetworkMaintenanceState::OtaPending;
    for (size_t index = 0; jobs_ != nullptr && index < kNetworkJobCapacity;
         ++index) {
      NetworkJobSnapshot &job = jobs_[index].snapshot;
      if (job.state == NetworkJobState::Running &&
          job.job_class == NetworkJobClass::Normal) {
        job.cancellation_requested = true;
      }
    }
    wake_handler = wake_handler_;
    wake_context = wake_context_;
  }
  if (wake_handler != nullptr) {
    wake_handler(wake_context);
  }
  return true;
}

bool NetworkJobManager::activate_ota_maintenance(std::string &error) {
  NetworkJobWakeHandler wake_handler = nullptr;
  void *wake_context = nullptr;
  {
    StaticLockGuard lock(mutex_);
    error.clear();
    if (maintenance_state_ == NetworkMaintenanceState::Normal) {
      error = "network_ota_maintenance_not_pending";
      return false;
    }
    if (external_work_id_ != 0) {
      error = "network_external_work_retirement_pending";
      return false;
    }
    maintenance_state_ = NetworkMaintenanceState::OtaExclusive;
    wake_handler = wake_handler_;
    wake_context = wake_context_;
  }
  if (wake_handler != nullptr) {
    wake_handler(wake_context);
  }
  return true;
}

void NetworkJobManager::end_ota_maintenance() {
  NetworkJobWakeHandler wake_handler = nullptr;
  void *wake_context = nullptr;
  {
    StaticLockGuard lock(mutex_);
    if (maintenance_state_ == NetworkMaintenanceState::Normal) {
      return;
    }
    maintenance_state_ = NetworkMaintenanceState::Normal;
    wake_handler = wake_handler_;
    wake_context = wake_context_;
  }
  if (wake_handler != nullptr) {
    wake_handler(wake_context);
  }
}

NetworkMaintenanceState NetworkJobManager::maintenance_state() const {
  StaticLockGuard lock(mutex_);
  return maintenance_state_;
}

uint32_t NetworkJobManager::read_clock_ms() const {
  return clock_ != nullptr ? clock_() : monotonic_ms();
}
bool NetworkJobManager::run_next(uint32_t now_ms, bool ota_retirement_only) {
  NetworkJobRunner runner = nullptr;
  void *context = nullptr;
  NetworkJobHandle handle;
  OperationContext execution;
  uint32_t queued_ms = 0;
#ifdef ESP_PLATFORM
  const bool profile = lunaredu::system::worker_work_profiling_active();
  char profile_owner[64]{}, profile_kind[64]{};
#endif
  {
    StaticLockGuard lock(mutex_);
    if (ota_retirement_only && maintenance_state_ != NetworkMaintenanceState::OtaPending) return false;
    const int index = next_queued_locked(now_ms, ota_retirement_only);
    if (index < 0) {
      return false;
    }
    JobRecord &record = jobs_[static_cast<size_t>(index)];
    record.snapshot.state = NetworkJobState::Running;
    record.snapshot.phase = NetworkJobPhase::Unspecified;
    record.snapshot.started_at_ms = now_ms;
    ++record.snapshot.attempts;
    runner = record.runner;
    execution=record.execution;
    context = record.context;
    handle = record.snapshot.handle;
    queued_ms = record.snapshot.queued_at_ms;
#ifdef ESP_PLATFORM
    if (profile) {
      std::memcpy(profile_owner, record.snapshot.owner_service_id.data(),
          std::min(record.snapshot.owner_service_id.size(), sizeof(profile_owner)-1));
      std::memcpy(profile_kind, record.snapshot.kind.data(),
          std::min(record.snapshot.kind.size(), sizeof(profile_kind)-1));
    }
#endif
  }

  NetworkJobRunResult result;
  const uint32_t started_ms = read_clock_ms();
  const uint32_t queue_wait_ms = started_ms - queued_ms;
#ifdef ESP_PLATFORM
  const auto started_task_us = lunaredu::system::current_task_runtime_us(profile);
#endif
  {
    RunnerContextGuard runner_context(*this, handle);
    OperationContextScope operation_context(execution,false);
    if(execution.cancelled())result={false,false,"operation_cancelled_before_start"};
    else if(execution.expired())result={false,false,"operation_deadline_exceeded_before_start"};
    else result = runner(handle, context);
  }
  const uint32_t elapsed_ms =
      read_clock_ms() - started_ms;
  const uint32_t completed_ms = now_ms + elapsed_ms;
#ifdef ESP_PLATFORM
  if (profile) {
    const auto task_us = uint32_t(lunaredu::system::current_task_runtime_us(true)-started_task_us);
    // Only registry labels, never endpoints, payloads or credentials.
    ESP_LOGI("network_job", "JOB_PROFILE id=%lu owner=%s kind=%s queue_ms=%lu wall_ms=%lu task_us=%lu ok=%d",
        static_cast<unsigned long>(handle.id), profile_owner, profile_kind,
        static_cast<unsigned long>(queue_wait_ms), static_cast<unsigned long>(elapsed_ms), static_cast<unsigned long>(task_us), result.succeeded);
  }
#endif

  StaticLockGuard lock(mutex_);
  const int index = find_locked(handle);
  if (index < 0) {
    return true;
  }
  JobRecord &record = jobs_[static_cast<size_t>(index)];
  NetworkJobSnapshot &job = record.snapshot;
  job.last_run_elapsed_ms = elapsed_ms;
  job.last_queue_wait_ms = queue_wait_ms;
  metrics_.maximum_queue_wait_ms = std::max(metrics_.maximum_queue_wait_ms, queue_wait_ms);
  ++metrics_.runner_calls;
  metrics_.runner_elapsed_ms += elapsed_ms;
  metrics_.maximum_run_elapsed_ms =
      std::max(metrics_.maximum_run_elapsed_ms, elapsed_ms);
  last_run_ = {};
  last_run_.handle = handle;
  last_run_.elapsed_ms = elapsed_ms;
  last_run_.queue_wait_ms = queue_wait_ms;
  std::memcpy(last_run_.owner_service_id.data(), job.owner_service_id.data(),
      std::min(job.owner_service_id.size(), last_run_.owner_service_id.size() - 1));
  std::memcpy(last_run_.kind.data(), job.kind.data(),
      std::min(job.kind.size(), last_run_.kind.size() - 1));
  job.detail = result.detail;
  if (job.cancellation_requested) {
    job.state = NetworkJobState::Cancelled;
    ++metrics_.cancelled;
  } else if (result.succeeded) {
    job.state = NetworkJobState::Succeeded;
  } else if (result.retryable && job.attempts < job.maximum_attempts &&
             (job.deadline_ms == 0 ||
              !internal::network_job_time_reached(completed_ms,
                                                  job.deadline_ms))) {
    job.state = NetworkJobState::Queued;
    job.queued_at_ms = started_ms + elapsed_ms;
    job.sequence = next_sequence_++;
    job.dispatch_wait_count = 0;
    job.ready_at_ms =
        completed_ms + internal::network_job_retry_backoff_ms(
                     record.retry_backoff_initial_ms,
                     record.retry_backoff_maximum_ms, job.attempts);
    ++metrics_.retried;
  } else {
    job.state = NetworkJobState::Failed;
  }
  if (internal::network_job_terminal(job.state)) {
    --active_count_;
    ++metrics_.completed;
  }
  if (!record.retain_terminal &&
      internal::network_job_terminal(job.state)) {
    record = {};
    --occupied_count_;
  }
  return true;
}

bool NetworkJobManager::release(NetworkJobHandle handle, std::string &error) {
  StaticLockGuard lock(mutex_);
  error.clear();
  const int index = find_locked(handle);
  if (index < 0) {
    error = "network_job_not_found";
    return false;
  }
  JobRecord &record = jobs_[static_cast<size_t>(index)];
  if (!internal::network_job_terminal(record.snapshot.state)) {
    error = "network_job_not_finished";
    return false;
  }
  record = {};
  --occupied_count_;
  return true;
}

bool network_job_runner_context_active() { return g_runner_context != nullptr; }
bool network_job_runner_accepts_normal_work() {
  return g_runner_context != nullptr && g_runner_context->accepts_normal_work();
}
bool network_job_runner_should_yield_transport_setup() {
  return g_runner_context != nullptr && g_runner_context->should_yield();
}
bool network_job_runner_cancellation_requested() {
  return operation_cancellation_requested()||(g_runner_context != nullptr && g_runner_context->cancellation_requested());
}
uint32_t network_job_runner_remaining_timeout_ms(uint32_t ceiling_ms) {
  const auto budget=operation_remaining_timeout_ms(ceiling_ms);
  return g_runner_context?g_runner_context->remaining_timeout_ms(budget):budget;
}
void network_job_runner_set_phase(NetworkJobPhase phase) {
  if (g_runner_context != nullptr) g_runner_context->set_phase(phase);
}

} // namespace lunaredu::lunaros
