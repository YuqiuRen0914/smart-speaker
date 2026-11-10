#include <cassert>
#include <cstdint>
#include <iostream>
#include <limits>

#include "network/network_job_manager_internal.h"

using namespace lunaredu::lunaros;

namespace {

NetworkJobRunResult noop(NetworkJobHandle, void *) { return {}; }

void test_policy_normalization() {
  NetworkJobPolicy policy;
  policy.maximum_active_jobs = 100;
  policy.maximum_jobs_per_owner = 100;
  policy.default_retry_backoff_ms = 0;
  policy.maximum_retry_backoff_ms = 0;
  policy = internal::normalize_network_job_policy(policy);
  assert(policy.maximum_active_jobs == kNetworkJobCapacity);
  assert(policy.maximum_jobs_per_owner == kNetworkJobCapacity);
  assert(policy.default_retry_backoff_ms == 1);
  assert(policy.maximum_retry_backoff_ms == 1);
}

void test_definition_validation() {
  NetworkJobDefinition valid{"owner", "kind", 0, 1, 0, noop, nullptr};
  assert(internal::valid_network_job_definition(valid));
  valid.owner_service_id = "";
  assert(!internal::valid_network_job_definition(valid));
  valid.owner_service_id = "owner";
  valid.runner = nullptr;
  assert(!internal::valid_network_job_definition(valid));
}

void test_retry_backoff_is_bounded() {
  assert(internal::network_job_retry_backoff_ms(100, 1000, 1) == 100);
  assert(internal::network_job_retry_backoff_ms(100, 1000, 4) == 800);
  assert(internal::network_job_retry_backoff_ms(100, 1000, 8) == 1000);
}

void test_ageing_boost_is_bounded() {
  NetworkJobSnapshot job;
  job.priority = 200;
  job.dispatch_wait_count = std::numeric_limits<uint32_t>::max();
  NetworkJobPolicy policy;
  policy.ageing_boost_per_dispatch = 100;
  policy.maximum_ageing_boost = 55;
  assert(internal::effective_network_job_priority(job, policy) == 255);
}

void test_time_comparison_handles_wraparound() {
  const uint32_t target = std::numeric_limits<uint32_t>::max() - 20;
  assert(!internal::network_job_time_reached(target - 1, target));
  assert(internal::network_job_time_until(target - 1, target) == 1);
  assert(internal::network_job_time_reached(5, target));
}

} // namespace

int main() {
  test_policy_normalization();
  test_definition_validation();
  test_retry_backoff_is_bounded();
  test_ageing_boost_is_bounded();
  test_time_comparison_handles_wraparound();
  std::cout << "network job policy tests passed: 5\n";
  return 0;
}
