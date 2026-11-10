#pragma once

#include "kernel/static_mutex.h"
#include "kernel/registration_storage.h"
#include <array>
#include <cstddef>
#include <cstdint>

namespace lunaredu::lunaros {

enum class ExecutionClass : uint8_t { Unspecified, Background, Deadline };
// Non-deadline work may explicitly migrate. Its interference applies to every
// core; this is never a promise of a per-core execution budget.
inline constexpr int32_t kExecutionAnyCore = -1;
// Effects describe resources, never concrete applications or services.
enum class ExecutionEffect : uint32_t {
  FlashWrite = 1, Reset = 2, UnboundedIo = 4, ActuatorTest = 8
};
constexpr uint32_t execution_effect(ExecutionEffect effect) {
  return static_cast<uint32_t>(effect);
}

struct ExecutionContract {
  ExecutionClass kind = ExecutionClass::Unspecified;
  uint32_t period_us = 0;
  uint32_t deadline_us = 0;
  uint32_t execution_us = 0;
  uint32_t blocking_us = 0;
  uint32_t jitter_us = 0;
  uint32_t priority = 0;
  int32_t core = 0;
  uint32_t effects = 0;
  uint32_t excludes = 0;
  // Explicitly attests bounds on blocking/interference, not flight acceptance.
  bool bounded = false;
};
struct ExecutionPlatform {
  struct Core {
    bool bounded = false;
    uint32_t interference_period_us = 0;
    uint32_t interference_execution_us = 0;
    uint32_t nonpreemptible_us = 0;
  };
  uint8_t core_count = 2;
  std::array<Core, 2> cores{};
  bool cross_core_interference_bounded = false;
  uint32_t shared_blocking_us = 0;
  uint32_t deadline_excludes = execution_effect(ExecutionEffect::FlashWrite) |
      execution_effect(ExecutionEffect::Reset) | execution_effect(ExecutionEffect::UnboundedIo);
};
struct ExecutionLease { uint64_t generation = 0; };
struct ExecutionObservation {
  uint32_t response_bound_us = 0;
  const char *reason = nullptr;
};

// Reservations survive pending startup/cleanup. No implicit release on expiry.
class ExecutionAdmission {
public:
  bool configure(const ExecutionPlatform &, const char *&error);
  bool reserve(const char *owner, const ExecutionContract &, ExecutionLease &,
               const char *&error, bool fixed_schedule = false);
  bool replace(const char *owner, ExecutionLease, const ExecutionContract &,
               const char *&error);
  bool release(const char *owner, ExecutionLease &);
  bool can_reset() const;
  bool observe(const char *owner, ExecutionLease, ExecutionObservation &) const;

private:
  struct Entry {
    std::array<char, 48> owner{};
    ExecutionContract contract;
    uint64_t generation = 0;
    bool fixed_schedule = false;
  };
  bool evaluate(const ExecutionPlatform &, const Entry &, size_t replacing,
                ExecutionObservation &) const;
  bool compatible(const Entry &, size_t replacing, const char *&error);
  mutable StaticMutex mutex_;
  ExecutionPlatform platform_;
  RegistrationTable<Entry> entries_{true};
  uint64_t generation_ = 0;
};

struct ExecutionTiming {
  uint64_t iterations = 0, deadline_misses = 0, budget_overruns = 0;
  uint32_t maximum_execution_us = 0, maximum_response_us = 0;
};
// Owner-local runtime evidence. Timing observation does not enforce CPU quotas.
class ExecutionMonitor {
public:
  bool finish(const ExecutionContract &, uint64_t release_us,
              uint64_t start_us, uint64_t end_us);
  ExecutionTiming snapshot() const;
  void reset();
private:
  ExecutionTiming timing_;
};

} // namespace lunaredu::lunaros
