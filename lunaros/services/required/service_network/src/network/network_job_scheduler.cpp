#include "lunaros_network_jobs.h"

#include <algorithm>
#include <limits>

#include "network/network_job_manager_internal.h"

namespace lunaredu::lunaros {

uint32_t NetworkJobManager::next_wake_delay_ms(uint32_t now_ms) const {
  StaticLockGuard lock(mutex_);
  if(!active_count_)return std::numeric_limits<uint32_t>::max();
  uint32_t delay = std::numeric_limits<uint32_t>::max();
  for (size_t index = 0; jobs_ != nullptr && index < kNetworkJobCapacity;
       ++index) {
    const JobRecord &record = jobs_[index];
    const NetworkJobSnapshot &job = record.snapshot;
    if (job.state != NetworkJobState::Queued || !job_runnable_locked(job)) {
      continue;
    }
    if (job.deadline_ms != 0) {
      delay = std::min(
          delay,
          internal::network_job_time_until(now_ms, job.deadline_ms));
    }
    delay = std::min(
        delay, internal::network_job_ready_delay_ms(now_ms, job));
  }
  return delay;
}

NetworkJobMetrics NetworkJobManager::metrics() const {
  StaticLockGuard lock(mutex_);return metrics_;
}
NetworkJobManagerSnapshot NetworkJobManager::snapshot() const {
  StaticLockGuard lock(mutex_);
  NetworkJobManagerSnapshot result;
  result.policy = policy_;
  result.metrics = metrics_;
  result.last_run = last_run_;
  result.maintenance_state = maintenance_state_;
  result.retirement_window_count = retirement_window_count_;
  result.external_normal_work = external_work_id_ != 0;
  for (size_t index = 0; jobs_ != nullptr && index < kNetworkJobCapacity;
       ++index) {
    const JobRecord &record = jobs_[index];
    if (record.snapshot.state == NetworkJobState::Free) {
      continue;
    }
    result.jobs[result.record_count++] = record.snapshot;
    if (record.snapshot.state == NetworkJobState::Queued) {
      ++result.queued_count;
      ++result.active_count;
    } else if (record.snapshot.state == NetworkJobState::Running) {
      ++result.running_count;
      ++result.active_count;
    } else if (internal::network_job_terminal(record.snapshot.state)) {
      ++result.terminal_count;
    }
  }
  return result;
}

int NetworkJobManager::find_locked(NetworkJobHandle handle) const {
  if (!handle) {
    return -1;
  }
  for (size_t index = 0; jobs_ != nullptr && index < kNetworkJobCapacity;
       ++index) {
    if (jobs_[index].snapshot.state != NetworkJobState::Free &&
        jobs_[index].snapshot.handle.id == handle.id) {
      return static_cast<int>(index);
    }
  }
  return -1;
}

int NetworkJobManager::next_queued_locked(uint32_t now_ms, bool retirement_only) {
  int selected = -1;
  for (size_t index = 0; jobs_ != nullptr && index < kNetworkJobCapacity;
       ++index) {
    JobRecord &record = jobs_[index];
    NetworkJobSnapshot &job = record.snapshot;
    if (retirement_only && job.job_class != NetworkJobClass::Retirement) continue;
    if (job.state != NetworkJobState::Queued) {
      continue;
    }
    if (job.deadline_ms != 0 &&
        internal::network_job_time_reached(now_ms, job.deadline_ms)) {
      job.state = NetworkJobState::Failed;
      job.detail = "deadline_exceeded";
      --active_count_;
      ++metrics_.deadline_exceeded;
      ++metrics_.completed;
      if (!record.retain_terminal) {
        record = {};
        --occupied_count_;
      }
      continue;
    }
    if (!job_runnable_locked(job)) {
      continue;
    }
    if (internal::network_job_ready_delay_ms(now_ms, job) != 0) {
      continue;
    }
    const uint16_t job_priority =
        internal::effective_network_job_priority(job, policy_);
    const uint16_t selected_priority =
        selected < 0
            ? 0
            : internal::effective_network_job_priority(
                  jobs_[static_cast<size_t>(selected)].snapshot, policy_);
    const bool retirement = job.job_class == NetworkJobClass::Retirement;
    const bool selected_retirement = selected >= 0 &&
        jobs_[static_cast<size_t>(selected)].snapshot.job_class == NetworkJobClass::Retirement;
    // Release occupied resources before admitting more work, irrespective of
    // ordinary priority/ageing. FIFO and bounded ageing still apply within a class.
    if (selected < 0 || (retirement && !selected_retirement) ||
        (retirement == selected_retirement &&
         (job_priority > selected_priority || (job_priority == selected_priority &&
         job.sequence <
             jobs_[static_cast<size_t>(selected)].snapshot.sequence)))) {
      selected = static_cast<int>(index);
    }
  }
  if (selected >= 0) {
    for (size_t index = 0; index < kNetworkJobCapacity; ++index) {
      NetworkJobSnapshot &job = jobs_[index].snapshot;
      if (static_cast<int>(index) != selected &&
          job.state == NetworkJobState::Queued &&
          job_runnable_locked(job) &&
          internal::network_job_ready_delay_ms(now_ms, job) == 0 &&
          job.dispatch_wait_count != UINT32_MAX) {
        ++job.dispatch_wait_count;
      }
    }
  }
  return selected;
}

bool NetworkJobManager::job_runnable_locked(
    const NetworkJobSnapshot &job) const {
  if (job.job_class == NetworkJobClass::Retirement) return true;
  if (retirement_window_count_ != 0) return false;
  if (maintenance_state_ == NetworkMaintenanceState::OtaPending) {
    return false;
  }
  if (maintenance_state_ == NetworkMaintenanceState::OtaExclusive) {
    return job.job_class == NetworkJobClass::Ota;
  }
  return job.job_class == NetworkJobClass::Normal;
}

size_t NetworkJobManager::active_count_locked() const { return active_count_; }

size_t NetworkJobManager::owner_active_count_locked(
  const char *owner_service_id) const {
  size_t count = 0;
  for (size_t index = 0; jobs_ != nullptr && index < kNetworkJobCapacity;
       ++index) {
    const JobRecord &record = jobs_[index];
    if ((record.snapshot.state == NetworkJobState::Queued ||
         record.snapshot.state == NetworkJobState::Running) &&
        record.snapshot.owner_service_id == owner_service_id) {
      ++count;
    }
  }
  return count;
}

} // namespace lunaredu::lunaros
