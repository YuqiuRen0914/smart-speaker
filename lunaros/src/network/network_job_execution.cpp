#include "lunaredu_lunaros/network/network_jobs.h"

#include "network/network_job_manager_internal.h"
#include <chrono>

namespace lunaredu::lunaros {
namespace {

thread_local uint32_t g_runner_depth = 0;

uint32_t monotonic_ms() {
  return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count());
}

class RunnerContextGuard {
public:
  RunnerContextGuard() { ++g_runner_depth; }
  ~RunnerContextGuard() { --g_runner_depth; }
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
      ++metrics_.cancelled;
      ++metrics_.completed;
      if (!record.retain_terminal) {
        record = {};
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

bool NetworkJobManager::run_next(uint32_t now_ms) {
  NetworkJobRunner runner = nullptr;
  void *context = nullptr;
  NetworkJobHandle handle;
  {
    StaticLockGuard lock(mutex_);
    const int index = next_queued_locked(now_ms);
    if (index < 0) {
      return false;
    }
    JobRecord &record = jobs_[static_cast<size_t>(index)];
    record.snapshot.state = NetworkJobState::Running;
    ++record.snapshot.attempts;
    runner = record.runner;
    context = record.context;
    handle = record.snapshot.handle;
  }

  NetworkJobRunResult result;
  const uint32_t started_ms = clock_ != nullptr ? clock_() : monotonic_ms();
  {
    RunnerContextGuard runner_context;
    result = runner(handle, context);
  }
  const uint32_t completed_ms = now_ms +
      ((clock_ != nullptr ? clock_() : monotonic_ms()) - started_ms);

  StaticLockGuard lock(mutex_);
  const int index = find_locked(handle);
  if (index < 0) {
    return true;
  }
  JobRecord &record = jobs_[static_cast<size_t>(index)];
  NetworkJobSnapshot &job = record.snapshot;
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
    ++metrics_.completed;
  }
  if (!record.retain_terminal &&
      internal::network_job_terminal(job.state)) {
    record = {};
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
  return true;
}

bool network_job_runner_context_active() { return g_runner_depth != 0; }

} // namespace lunaredu::lunaros
