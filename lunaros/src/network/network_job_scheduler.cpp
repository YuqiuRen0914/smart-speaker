#include "lunaredu_lunaros/network/network_jobs.h"

#include <algorithm>
#include <limits>

#include "network/network_job_manager_internal.h"

namespace lunaredu::lunaros {

uint32_t NetworkJobManager::next_wake_delay_ms(uint32_t now_ms) const {
  StaticLockGuard lock(mutex_);
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
        delay, internal::network_job_time_until(now_ms, job.ready_at_ms));
  }
  return delay;
}

NetworkJobManagerSnapshot NetworkJobManager::snapshot() const {
  StaticLockGuard lock(mutex_);
  NetworkJobManagerSnapshot result;
  result.policy = policy_;
  result.metrics = metrics_;
  result.maintenance_state = maintenance_state_;
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

int NetworkJobManager::next_queued_locked(uint32_t now_ms) {
  int selected = -1;
  for (size_t index = 0; jobs_ != nullptr && index < kNetworkJobCapacity;
       ++index) {
    JobRecord &record = jobs_[index];
    NetworkJobSnapshot &job = record.snapshot;
    if (job.state != NetworkJobState::Queued) {
      continue;
    }
    if (job.deadline_ms != 0 &&
        internal::network_job_time_reached(now_ms, job.deadline_ms)) {
      job.state = NetworkJobState::Failed;
      job.detail = "deadline_exceeded";
      ++metrics_.deadline_exceeded;
      ++metrics_.completed;
      if (!record.retain_terminal) {
        record = {};
      }
      continue;
    }
    if (!job_runnable_locked(job)) {
      continue;
    }
    if (!internal::network_job_time_reached(now_ms, job.ready_at_ms)) {
      continue;
    }
    const uint16_t job_priority =
        internal::effective_network_job_priority(job, policy_);
    const uint16_t selected_priority =
        selected < 0
            ? 0
            : internal::effective_network_job_priority(
                  jobs_[static_cast<size_t>(selected)].snapshot, policy_);
    if (selected < 0 || job_priority > selected_priority ||
        (job_priority == selected_priority &&
         job.sequence <
             jobs_[static_cast<size_t>(selected)].snapshot.sequence)) {
      selected = static_cast<int>(index);
    }
  }
  if (selected >= 0) {
    for (size_t index = 0; index < kNetworkJobCapacity; ++index) {
      NetworkJobSnapshot &job = jobs_[index].snapshot;
      if (static_cast<int>(index) != selected &&
          job.state == NetworkJobState::Queued &&
          job_runnable_locked(job) &&
          internal::network_job_time_reached(now_ms, job.ready_at_ms) &&
          job.dispatch_wait_count != UINT32_MAX) {
        ++job.dispatch_wait_count;
      }
    }
  }
  return selected;
}

bool NetworkJobManager::job_runnable_locked(
    const NetworkJobSnapshot &job) const {
  if (maintenance_state_ == NetworkMaintenanceState::OtaPending) {
    return false;
  }
  if (maintenance_state_ == NetworkMaintenanceState::OtaExclusive) {
    return job.job_class == NetworkJobClass::Ota;
  }
  return job.job_class == NetworkJobClass::Normal;
}

size_t NetworkJobManager::active_count_locked() const {
  size_t count = 0;
  for (size_t index = 0; jobs_ != nullptr && index < kNetworkJobCapacity;
       ++index) {
    const JobRecord &record = jobs_[index];
    if (record.snapshot.state == NetworkJobState::Queued ||
        record.snapshot.state == NetworkJobState::Running) {
      ++count;
    }
  }
  return count;
}

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
