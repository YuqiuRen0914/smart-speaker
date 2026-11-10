#include "network/network_job_manager_internal.h"

#include <algorithm>

namespace lunaredu::lunaros::internal {

NetworkJobPolicy normalize_network_job_policy(NetworkJobPolicy policy) {
  policy.maximum_active_jobs =
      std::min(policy.maximum_active_jobs, kNetworkJobCapacity);
  policy.maximum_jobs_per_owner =
      std::min(policy.maximum_jobs_per_owner, kNetworkJobCapacity);
  if (policy.default_retry_backoff_ms == 0) {
    policy.default_retry_backoff_ms = 1;
  }
  policy.maximum_retry_backoff_ms = std::max(
      policy.default_retry_backoff_ms, policy.maximum_retry_backoff_ms);
  return policy;
}

bool valid_network_job_definition(const NetworkJobDefinition &definition) {
  const bool valid_class =
      definition.job_class == NetworkJobClass::Normal ||
      definition.job_class == NetworkJobClass::Ota;
  return definition.owner_service_id != nullptr &&
         definition.owner_service_id[0] != '\0' && definition.kind != nullptr &&
         definition.kind[0] != '\0' && definition.runner != nullptr &&
         definition.maximum_attempts > 0 && valid_class;
}

bool network_job_terminal(NetworkJobState state) {
  return state == NetworkJobState::Succeeded ||
         state == NetworkJobState::Failed ||
         state == NetworkJobState::Cancelled;
}

bool network_job_time_reached(uint32_t now_ms, uint32_t target_ms) {
  return target_ms == 0 || static_cast<int32_t>(now_ms - target_ms) >= 0;
}

uint32_t network_job_time_until(uint32_t now_ms, uint32_t target_ms) {
  return network_job_time_reached(now_ms, target_ms) ? 0
                                                      : target_ms - now_ms;
}

uint16_t effective_network_job_priority(const NetworkJobSnapshot &job,
                                        const NetworkJobPolicy &policy) {
  const uint64_t raw_boost =
      static_cast<uint64_t>(job.dispatch_wait_count) *
      policy.ageing_boost_per_dispatch;
  const uint64_t boost =
      std::min<uint64_t>(raw_boost, policy.maximum_ageing_boost);
  return static_cast<uint16_t>(job.priority) + static_cast<uint16_t>(boost);
}

uint32_t network_job_retry_backoff_ms(uint32_t initial_ms,
                                      uint32_t maximum_ms,
                                      uint8_t attempts) {
  uint64_t delay = initial_ms;
  for (uint8_t attempt = 1; attempt < attempts; ++attempt) {
    delay = std::min<uint64_t>(delay * 2, maximum_ms);
  }
  return static_cast<uint32_t>(std::min<uint64_t>(delay, maximum_ms));
}

} // namespace lunaredu::lunaros::internal
