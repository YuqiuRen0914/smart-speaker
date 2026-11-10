#include <array>
#include <iostream>
#include <string>

#include "network/network_job_manager_internal.h"

namespace {

int g_failures = 0;
uint32_t g_clock_ms = 0;

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::cerr << "CHECK failed line=" << __LINE__ << ": " #condition       \
                << std::endl;                                                  \
      ++g_failures;                                                            \
    }                                                                          \
  } while (false)

struct RunContext {
  int calls = 0;
  bool retry_once = false;
  bool observed_runner_context = false;
};

struct WakeContext {
  lunaredu::lunaros::NetworkJobManager *manager = nullptr;
  int calls = 0;
};

lunaredu::lunaros::NetworkJobRunResult run_job(
    lunaredu::lunaros::NetworkJobHandle, void *raw_context) {
  auto *context = static_cast<RunContext *>(raw_context);
  ++context->calls;
  context->observed_runner_context =
      lunaredu::lunaros::network_job_runner_context_active();
  if (context->retry_once && context->calls == 1) {
    return {false, true, "retry"};
  }
  return {true, false, "done"};
}

void inspect_on_wake(void *raw_context) {
  auto *context = static_cast<WakeContext *>(raw_context);
  ++context->calls;
  // This intentionally re-enters the manager. It would deadlock if submit()
  // invoked the wake callback while holding the scheduler mutex.
  (void)context->manager->snapshot();
}

void test_priority_retry_cancel_and_deadline() {
  lunaredu::lunaros::NetworkJobManager manager([] { return 0U; });
  lunaredu::lunaros::NetworkJobPolicy policy;
  policy.maximum_active_jobs = 3;
  policy.maximum_jobs_per_owner = 3;
  policy.default_retry_backoff_ms = 10;
  policy.maximum_retry_backoff_ms = 20;
  manager.reset(policy);
  WakeContext wake{&manager};
  manager.set_wake_handler(inspect_on_wake, &wake);
  std::string error;
  RunContext low;
  RunContext high{0, true};
  lunaredu::lunaros::NetworkJobHandle low_handle;
  lunaredu::lunaros::NetworkJobHandle high_handle;
  CHECK(manager.submit({"information.runtime", "weather", 10, 1, 0,
                        run_job, &low, true},
                       low_handle, error));
  CHECK(manager.submit({"cloud.device_link", "presence", 90, 2, 0,
                        run_job, &high, true},
                       high_handle, error));

  CHECK(manager.run_next(100));
  CHECK(high.calls == 1);
  CHECK(high.observed_runner_context);
  CHECK(low.calls == 0);
  CHECK(manager.run_next(101));
  CHECK(low.calls == 1);
  CHECK(low.observed_runner_context);
  CHECK(!manager.run_next(102));
  CHECK(manager.next_wake_delay_ms(102) == 8);
  CHECK(manager.run_next(110));
  CHECK(high.calls == 2);
  CHECK(wake.calls == 2);

  RunContext cancelled;
  lunaredu::lunaros::NetworkJobHandle cancelled_handle;
  CHECK(manager.submit({"resource.delivery", "download", 20, 1, 0,
                        run_job, &cancelled, true},
                       cancelled_handle, error));
  CHECK(wake.calls == 3);
  CHECK(manager.cancel(cancelled_handle, error));
  CHECK(wake.calls == 4);
  CHECK(!manager.run_next(111));
  CHECK(cancelled.calls == 0);

  CHECK(manager.release(high_handle, error));
  CHECK(manager.release(low_handle, error));
  CHECK(manager.release(cancelled_handle, error));

  RunContext expired;
  lunaredu::lunaros::NetworkJobHandle expired_handle;
  CHECK(manager.submit({"system.update", "manifest", 50, 1, 200,
                        run_job, &expired, true},
                       expired_handle, error));
  CHECK(!manager.run_next(200));
  CHECK(expired.calls == 0);
  auto snapshot = manager.snapshot();
  CHECK(snapshot.record_count == 1);
  CHECK(snapshot.active_count == 0);
  CHECK(snapshot.jobs[0].state ==
        lunaredu::lunaros::NetworkJobState::Failed);
  CHECK(snapshot.jobs[0].detail == "deadline_exceeded");
  CHECK(snapshot.metrics.retried == 1);
  CHECK(snapshot.metrics.cancelled == 1);
  CHECK(snapshot.metrics.deadline_exceeded == 1);
  CHECK(snapshot.metrics.completed == 4);
}

void test_owner_admission_and_ageing() {
  lunaredu::lunaros::NetworkJobManager manager([] { return 0U; });
  lunaredu::lunaros::NetworkJobPolicy policy;
  policy.maximum_active_jobs = 8;
  policy.maximum_jobs_per_owner = 2;
  policy.ageing_boost_per_dispatch = 100;
  policy.maximum_ageing_boost = 255;
  manager.reset(policy);

  std::string error;
  RunContext low;
  RunContext high_one;
  RunContext high_two;
  RunContext rejected;
  lunaredu::lunaros::NetworkJobHandle low_handle;
  lunaredu::lunaros::NetworkJobHandle high_one_handle;
  lunaredu::lunaros::NetworkJobHandle high_two_handle;
  lunaredu::lunaros::NetworkJobHandle rejected_handle;

  CHECK(manager.submit({"background", "refresh", 0, 1, 0, run_job, &low,
                        true},
                       low_handle, error));
  CHECK(manager.submit({"interactive", "one", 200, 1, 0, run_job,
                        &high_one, true},
                       high_one_handle, error));
  CHECK(manager.submit({"interactive", "two", 200, 1, 0, run_job,
                        &high_two, true},
                       high_two_handle, error));
  CHECK(!manager.submit({"interactive", "three", 200, 1, 0, run_job,
                         &rejected, true},
                        rejected_handle, error));
  CHECK(error == "network_job_owner_limit");

  CHECK(manager.run_next(1));
  CHECK(high_one.calls == 1);
  CHECK(manager.run_next(2));
  CHECK(high_two.calls == 1);
  // Two dispatches add 200 ageing points, so the oldest background job now
  // ties priority 200 and wins FIFO instead of starving.
  CHECK(manager.run_next(3));
  CHECK(low.calls == 1);
  const auto snapshot = manager.snapshot();
  CHECK(snapshot.metrics.rejected_owner_limit == 1);
}

void test_ota_maintenance_admission_and_deferral() {
  using namespace lunaredu::lunaros;
  NetworkJobManager manager([] { return 0U; });
  NetworkJobPolicy policy;
  policy.maximum_active_jobs = 2;
  policy.maximum_jobs_per_owner = 2;
  manager.reset(policy);

  std::string error;
  RunContext ordinary;
  RunContext ota;
  NetworkJobHandle ordinary_handle;
  NetworkJobHandle ota_handle;
  NetworkJobDefinition ota_definition{
      "system.update", "firmware_pull", kNetworkPriorityCritical, 1, 0,
      run_job, &ota, true};
  ota_definition.job_class = NetworkJobClass::Ota;

  CHECK(manager.submit({"resource.delivery", "download",
                        kNetworkPriorityBackground, 1, 0, run_job, &ordinary,
                        true},
                       ordinary_handle, error));
  CHECK(!manager.submit(ota_definition, ota_handle, error));
  CHECK(error == "network_ota_maintenance_required");

  CHECK(manager.begin_ota_maintenance(error));
  CHECK(manager.maintenance_state() ==
        NetworkMaintenanceState::OtaPending);
  NetworkJobHandle rejected_handle;
  CHECK(!manager.submit({"information.runtime", "refresh",
                         kNetworkPriorityBackground, 1, 0, run_job, nullptr,
                         false},
                        rejected_handle, error));
  CHECK(error == "network_ota_maintenance_active");
  CHECK(manager.submit(ota_definition, ota_handle, error));
  CHECK(!manager.run_next(10));
  CHECK(ordinary.calls == 0);
  CHECK(ota.calls == 0);

  CHECK(manager.activate_ota_maintenance(error));
  CHECK(manager.run_next(11));
  CHECK(ota.calls == 1);
  CHECK(ordinary.calls == 0);
  manager.end_ota_maintenance();
  CHECK(manager.run_next(12));
  CHECK(ordinary.calls == 1);
  const auto snapshot = manager.snapshot();
  CHECK(snapshot.maintenance_state == NetworkMaintenanceState::Normal);
  CHECK(snapshot.metrics.rejected_maintenance == 2);
}

void test_ota_registry_slots_are_reserved() {
  using namespace lunaredu::lunaros;
  NetworkJobManager manager([] { return 0U; });
  NetworkJobPolicy policy;
  policy.maximum_active_jobs = kNetworkJobCapacity;
  policy.maximum_jobs_per_owner = kNetworkJobCapacity;
  manager.reset(policy);

  std::string error;
  RunContext ordinary;
  std::array<NetworkJobHandle,
             kNetworkJobCapacity - kNetworkOtaReservedSlots>
      ordinary_handles{};
  for (auto &handle : ordinary_handles) {
    CHECK(manager.submit({"background", "work", kNetworkPriorityBackground,
                          1, 0, run_job, &ordinary, true},
                         handle, error));
  }
  NetworkJobHandle rejected;
  CHECK(!manager.submit({"background", "overflow", kNetworkPriorityBackground,
                         1, 0, run_job, &ordinary, true},
                        rejected, error));
  CHECK(error == "network_job_ota_reserve");

  CHECK(manager.begin_ota_maintenance(error));
  CHECK(manager.activate_ota_maintenance(error));
  NetworkJobDefinition ota_definition{
      "system.update", "firmware_pull", kNetworkPriorityCritical, 1, 0,
      run_job, &ordinary, true};
  ota_definition.job_class = NetworkJobClass::Ota;
  NetworkJobHandle ota_one;
  NetworkJobHandle ota_two;
  CHECK(manager.submit(ota_definition, ota_one, error));
  CHECK(manager.submit(ota_definition, ota_two, error));
}

void test_retry_after_completion() {
  using namespace lunaredu::lunaros;
  NetworkJobManager manager([] { return g_clock_ms; });
  manager.reset();
  std::string error;
  auto slow_failure = [](NetworkJobHandle, void *) -> NetworkJobRunResult {
    g_clock_ms += 5000;
    return {false, true, "slow_failure"};
  };
  NetworkJobHandle handle;
  CHECK(manager.submit({"owner", "slow", 1, 2, 0, slow_failure, nullptr, true}, handle, error));
  CHECK(manager.run_next(100));
  CHECK(manager.next_wake_delay_ms(5100) == 1000);
  CHECK(!manager.run_next(6099));
  CHECK(manager.run_next(6100));
  CHECK(manager.release(handle, error));
  CHECK(manager.submit({"owner", "expired", 1, 2, 200, slow_failure, nullptr, true}, handle, error));
  CHECK(manager.run_next(100));
  CHECK(manager.snapshot().jobs[0].state == NetworkJobState::Failed);
  CHECK(manager.release(handle, error));
  // Unsigned elapsed-time arithmetic must also survive the clock wrapping.
  g_clock_ms = UINT32_MAX - 2000;
  CHECK(manager.submit({"owner", "wrap", 1, 2, 0, slow_failure, nullptr, true}, handle, error));
  CHECK(manager.run_next(100));
  CHECK(manager.next_wake_delay_ms(5100) == 1000);
}

} // namespace

void test_handles_remain_invalid_after_reset() {
  using namespace lunaredu::lunaros;
  NetworkJobManager manager;
  manager.reset();
  RunContext old_work, new_work;
  NetworkJobHandle old_handle, new_handle;
  std::string error;
  CHECK(manager.submit({"old_owner", "work", 1, 1, 0, run_job, &old_work, true}, old_handle, error));
  CHECK(manager.run_next(0));
  manager.reset();
  CHECK(manager.submit({"new_owner", "work", 1, 1, 0, run_job, &new_work, true}, new_handle, error));
  CHECK(old_handle.id != new_handle.id);
  CHECK(!manager.active(old_handle));
  CHECK(!manager.cancel(old_handle, error));
  CHECK(!manager.release(old_handle, error));
  CHECK(manager.active(new_handle));
  CHECK(manager.run_next(0));
  CHECK(new_work.calls == 1);
}

int main() {
  test_handles_remain_invalid_after_reset();
  test_retry_after_completion();
  test_priority_retry_cancel_and_deadline();
  test_owner_admission_and_ageing();
  test_ota_maintenance_admission_and_deferral();
  test_ota_registry_slots_are_reserved();
  if (g_failures != 0) {
    std::cerr << g_failures << " network job manager checks failed"
              << std::endl;
    return 1;
  }
  std::cout << "6 LunarOS network job manager checks passed" << std::endl;
  return 0;
}
