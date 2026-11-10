#include "kernel/execution_admission.h"
#include <algorithm>
#include <cstring>
#include <limits>

namespace lunaredu::lunaros {
namespace {
bool valid(const ExecutionContract &c) {
  if(static_cast<unsigned>(c.kind)>2)return false;
  if ((c.core < 0 && !(c.core==kExecutionAnyCore&&c.kind!=ExecutionClass::Deadline)) || c.core > 1) return false;
  if (c.kind == ExecutionClass::Unspecified) return true;
  if (!c.bounded) return false;
  if (c.kind == ExecutionClass::Deadline || c.execution_us != 0)
    return c.period_us && c.deadline_us && c.execution_us &&
           c.execution_us <= c.deadline_us && c.deadline_us <= c.period_us &&
           c.jitter_us < c.deadline_us;
  return c.period_us == 0 && c.deadline_us == 0;
}
uint64_t interference(uint64_t interval, uint32_t period, uint32_t cost) {
  return cost ? ((interval + period - 1) / period) * cost : 0;
}
bool owner_valid(const char *owner) {
  return owner && *owner && std::strlen(owner) < 48;
}
}

bool ExecutionAdmission::evaluate(const ExecutionPlatform &platform,
    const Entry &candidate, size_t replacing, ExecutionObservation &out) const {
  out = {};
  const auto &c = candidate.contract;
  if (!valid(c) || c.core >= platform.core_count) {
    out.reason = "execution_contract_invalid"; return false;
  }
  if (c.kind != ExecutionClass::Deadline) return true;
  if(platform.core_count>1 && !platform.cross_core_interference_bounded) {
    out.reason="execution_cross_core_unbounded";return false;
  }
  const auto &cpu = platform.cores[c.core];
  if (!cpu.bounded) { out.reason = "execution_platform_unbounded"; return false; }
  uint64_t blocking = std::max(c.blocking_us, cpu.nonpreemptible_us);
  for (size_t i = 0; i < entries_.size(); ++i) {
    const auto &other = entries_[i];
    if (!other.generation || i == replacing ||
        (other.contract.core != c.core&&other.contract.core!=kExecutionAnyCore)) continue;
    const auto &h = other.contract;
    if (h.kind == ExecutionClass::Unspecified || !h.bounded ||
        (h.priority >= c.priority && !h.period_us)) {
      out.reason = "execution_workload_unbounded"; return false;
    }
    blocking = std::max<uint64_t>(blocking, h.blocking_us);
  }
  blocking += platform.shared_blocking_us;
  uint64_t response = c.execution_us + blocking + c.jitter_us;
  for (unsigned iteration = 0; iteration < 64; ++iteration) {
    if (response > c.deadline_us) {
      out.reason = "execution_deadline_unschedulable"; return false;
    }
    uint64_t next = c.execution_us + blocking + c.jitter_us +
        interference(response, cpu.interference_period_us,
                     cpu.interference_execution_us);
    for (size_t i = 0; i < entries_.size(); ++i) {
      const auto &other = entries_[i];
      if (!other.generation || i == replacing ||
          (other.contract.core != c.core&&other.contract.core!=kExecutionAnyCore) ||
          other.contract.priority < c.priority) continue;
      next += interference(response + other.contract.jitter_us,
                           other.contract.period_us, other.contract.execution_us);
    }
    // The candidate is already present when observing existing reservations.
    if (next <= response) {
      out.response_bound_us = static_cast<uint32_t>(response); return true;
    }
    response = next;
  }
  out.reason = "execution_response_did_not_converge"; return false;
}

bool ExecutionAdmission::compatible(const Entry &candidate, size_t replacing,
                                    const char *&error) {
  for (size_t i = 0; i < entries_.size(); ++i) {
    const auto &other = entries_[i];
    if (!other.generation || i == replacing) continue;
    const auto candidate_excludes=candidate.contract.excludes |
        (candidate.contract.kind==ExecutionClass::Deadline?platform_.deadline_excludes:0);
    const auto other_excludes=other.contract.excludes |
        (other.contract.kind==ExecutionClass::Deadline?platform_.deadline_excludes:0);
    if ((candidate.contract.effects & other_excludes) ||
        (other.contract.effects & candidate_excludes)) {
      error = "execution_effect_conflict"; return false;
    }
  }
  ExecutionObservation result;
  if (!evaluate(platform_, candidate, replacing, result)) {
    error = result.reason; return false;
  }
  // Check the new work's impact on every existing deadline, atomically.
  const Entry saved = entries_[replacing];
  entries_[replacing] = candidate;
  entries_[replacing].generation = 1;
  bool ok = true;
  for (size_t i = 0; i < entries_.size(); ++i) {
    if (i == replacing || !entries_[i].generation) continue;
    if (!evaluate(platform_, entries_[i], i, result)) {
      error = result.reason; ok = false; break;
    }
  }
  entries_[replacing] = saved;
  return ok;
}

bool ExecutionAdmission::configure(const ExecutionPlatform &platform,
                                   const char *&error) {
  error = nullptr;
  if (!platform.core_count || platform.core_count > platform.cores.size()) {
    error = "execution_platform_invalid"; return false;
  }
  for (size_t i = 0; i < platform.core_count; ++i) {
    const auto &c = platform.cores[i];
    if (c.interference_execution_us && (!c.interference_period_us ||
        c.interference_execution_us >= c.interference_period_us)) {
      error = "execution_platform_invalid"; return false;
    }
  }
  StaticLockGuard lock(mutex_);
  ExecutionObservation out;
  for (size_t i = 0; i < entries_.size(); ++i) {
    if (entries_[i].generation && !evaluate(platform, entries_[i], i, out)) {
      error = out.reason; return false;
    }
    if(entries_[i].generation && entries_[i].contract.kind==ExecutionClass::Deadline) {
      for(size_t j=0;j<entries_.size();++j) {
        if(i!=j && entries_[j].generation && (entries_[j].contract.effects & platform.deadline_excludes)) {
          error="execution_effect_conflict";return false;
        }
      }
    }
  }
  platform_ = platform; return true;
}

bool ExecutionAdmission::reserve(const char *owner, const ExecutionContract &c,
                                 ExecutionLease &lease, const char *&error,bool fixed_schedule) {
  error = nullptr;
  if (!owner_valid(owner) || lease.generation) {
    error = "execution_reservation_invalid"; return false;
  }
  StaticLockGuard lock(mutex_);
  const size_t slot = entries_.size();
  if (generation_ == std::numeric_limits<uint64_t>::max()) {
    error = "execution_identity_exhausted"; return false;
  }
  if (!entries_.append()) {
    error = "execution_storage_unavailable"; return false;
  }
  Entry candidate;
  std::strcpy(candidate.owner.data(), owner); candidate.contract = c;
  candidate.fixed_schedule=fixed_schedule;
  if (!compatible(candidate, slot, error)) {
    entries_.erase(slot);
    return false;
  }
  if (++generation_ == 0) ++generation_;
  candidate.generation = generation_; entries_[slot] = candidate;
  lease.generation = generation_; return true;
}

bool ExecutionAdmission::replace(const char *owner, ExecutionLease lease,
    const ExecutionContract &c, const char *&error) {
  error = nullptr;
  if (!owner_valid(owner)) { error = "execution_owner_invalid"; return false; }
  StaticLockGuard lock(mutex_);
  for (size_t i = 0; i < entries_.size(); ++i) {
    auto &entry = entries_[i];
    if (!lease.generation || entry.generation != lease.generation ||
        std::strcmp(entry.owner.data(), owner)) continue;
    if(entry.fixed_schedule && (entry.contract.core!=c.core || entry.contract.priority!=c.priority)) {
      error="execution_schedule_immutable";return false;
    }
    Entry candidate = entry; candidate.contract = c;
    if (!compatible(candidate, i, error)) return false;
    entry = candidate; return true;
  }
  error = "execution_lease_not_found"; return false;
}

bool ExecutionAdmission::release(const char *owner, ExecutionLease &lease) {
  if (!owner_valid(owner) || !lease.generation) return false;
  StaticLockGuard lock(mutex_);
  for (size_t index = 0; index < entries_.size(); ++index) {
    auto &entry = entries_[index];
    if (entry.generation == lease.generation && !std::strcmp(entry.owner.data(), owner)) {
      entries_.erase(index); lease = {}; return true;
    }
  }
  return false;
}
bool ExecutionAdmission::can_reset() const {
  StaticLockGuard lock(mutex_);
  return entries_.size() == 0;
}
bool ExecutionAdmission::observe(const char *owner, ExecutionLease lease,
                                 ExecutionObservation &out) const {
  if (!owner_valid(owner) || !lease.generation) return false;
  StaticLockGuard lock(mutex_);
  for (size_t i = 0; i < entries_.size(); ++i)
    if (entries_[i].generation == lease.generation && !std::strcmp(entries_[i].owner.data(), owner))
      return evaluate(platform_, entries_[i], i, out);
  return false;
}

bool ExecutionMonitor::finish(const ExecutionContract &c, uint64_t release,
    uint64_t start, uint64_t end) {
  ++timing_.iterations;
  if (start < release || end < start) { ++timing_.deadline_misses; return false; }
  const auto cap = [](uint64_t n) { return static_cast<uint32_t>(
      std::min<uint64_t>(n, std::numeric_limits<uint32_t>::max())); };
  const auto execution = end - start, response = end - release;
  timing_.maximum_execution_us = std::max(timing_.maximum_execution_us, cap(execution));
  timing_.maximum_response_us = std::max(timing_.maximum_response_us, cap(response));
  const bool late = c.deadline_us && response > c.deadline_us;
  const bool over = c.execution_us && execution > c.execution_us;
  timing_.deadline_misses += late; timing_.budget_overruns += over;
  return !late && !over;
}
ExecutionTiming ExecutionMonitor::snapshot() const { return timing_; }
void ExecutionMonitor::reset() { timing_ = {}; }

} // namespace lunaredu::lunaros
