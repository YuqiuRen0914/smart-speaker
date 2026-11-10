#include "lunaros_network_scan.h"
#include "lunaros_network_lifecycle.h"
#include "kernel/runtime.h"
#include "network/network_scan_internal.h"
#include "network/wifi_scan_job.h"
#include "platform/cold_storage.h"
#include <algorithm>

namespace lunaredu::lunaros {
namespace {

LUNAROS_COLD_BSS NetworkWifiSurveySnapshot scan_view;
internal::WifiScanJob scan(scan_view);
constexpr uint32_t kScanQueueTimeoutMs = 15000;
NetworkJobRunResult run_wifi_scan(NetworkJobHandle handle, void *context) {
  return scan.run<false>(handle,static_cast<uint32_t>(reinterpret_cast<uintptr_t>(context)));
}
bool start_or_join_scan(uint32_t &generation, std::string &error) {
  return scan.start(run_wifi_scan,false,generation,error);
}

} // namespace

bool request_network_wifi_scan(KernelRuntime &runtime, std::string &error) {
  if (!require_network_runtime(runtime, error)) return false;
  uint32_t generation;
  return start_or_join_scan(generation, error);
}

bool request_network_wifi_scan_and_wait(KernelRuntime &runtime,
                                       NetworkWifiScanSnapshot &snapshot,
                                       std::string &error, uint32_t timeout_ms) {
  snapshot = {};
  error.clear();
  if (!require_network_runtime(runtime, error)) return false;
  if (runtime.current_task_owns_executor() || network_job_runner_context_active()) {
    error = "wifi_scan_wait_requires_external_caller";
    return false;
  }
  if (timeout_ms == 0 || timeout_ms > kScanQueueTimeoutMs) {
    error = "wifi_scan_invalid_timeout";
    return false;
  }
  uint32_t generation;
  const uint32_t started = system_monotonic_ms();
  if (!start_or_join_scan(generation, error)) return false;
  for (;;) {
    if (!scan.observe(generation,snapshot,error)) return false;
    if (snapshot.state != NetworkWifiScanState::Scanning) {
      error=snapshot.error.data();
      return snapshot.state==NetworkWifiScanState::Ready;
    }
    const uint32_t elapsed = system_monotonic_ms() - started;
    if (elapsed >= timeout_ms) {
      error = "wifi_scan_timeout";
      return false;
    }
    system_delay_ms(std::min<uint32_t>(10, timeout_ms - elapsed));
  }
}

NetworkWifiScanSnapshot network_wifi_scan_snapshot() {
  NetworkWifiScanSnapshot snapshot; scan.snapshot_into(snapshot); return snapshot;
}
void network_wifi_scan_snapshot_into(NetworkWifiScanSnapshot &snapshot) { scan.snapshot_into(snapshot); }
void reset_network_wifi_scan_internal() { scan.reset(); }
} // namespace lunaredu::lunaros
