#include "lunaredu_lunaros/network/network_commands.h"

#include <algorithm>
#include <cstdio>
#include <vector>

#include "kernel/static_mutex.h"
#include "kernel/system_primitives.h"
#include "lunaredu_lunaros/network/network_jobs.h"
#include "lunaredu_lunaros/kernel/service_executor.h"
#include "network/network_scan_internal.h"
#include "network/wifi/wifi_manager.h"

namespace lunaredu::lunaros {
namespace {

StaticMutex g_scan_mutex;
NetworkWifiScanSnapshot g_scan_snapshot;
NetworkJobHandle g_scan_job;
uint32_t g_generation = 0;
bool g_submitting = false;
constexpr uint32_t kScanQueueTimeoutMs = 15000;

void fail_locked(const char *error) {
  g_scan_snapshot.state = NetworkWifiScanState::Failed;
  g_scan_snapshot.access_point_count = 0;
  g_scan_snapshot.access_points = {};
  std::snprintf(g_scan_snapshot.error.data(), g_scan_snapshot.error.size(), "%s", error);
  ++g_scan_snapshot.sequence;
}

// A queued job can expire or be cancelled without entering its runner.
void reconcile_locked() {
  if (g_scan_snapshot.state == NetworkWifiScanState::Scanning &&
      !g_submitting && g_scan_job && !network_job_active(g_scan_job)) {
    fail_locked("wifi_scan_cancelled_or_expired");
  }
}

NetworkJobRunResult run_wifi_scan(NetworkJobHandle handle, void *context) {
  const auto generation = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(context));
  {
    StaticLockGuard lock(g_scan_mutex);
    if (generation != g_generation) return {false, false, "wifi_scan_superseded"};
  }
  std::vector<WifiScanApRecord> records;
  std::string error;
  bool succeeded = !network_job_cancellation_requested(handle);
  if (succeeded) succeeded = wifi_scan_access_points(records, error);
  if (network_job_cancellation_requested(handle)) {
    succeeded = false;
    error = "wifi_scan_cancelled";
  }
  StaticLockGuard lock(g_scan_mutex);
  if (generation != g_generation) return {false, false, "wifi_scan_superseded"};
  if (!succeeded && error == "wifi_radio_busy") {
    return {false, true, error}; // Retry without publishing a terminal failure.
  }
  if (!succeeded) {
    fail_locked(error.empty() ? "wifi_scan_failed" : error.c_str());
    return {false, false, g_scan_snapshot.error.data()};
  }
  ++g_scan_snapshot.sequence;
  g_scan_snapshot.state = NetworkWifiScanState::Ready;
  g_scan_snapshot.error[0] = '\0';
  g_scan_snapshot.access_point_count =
      std::min(records.size(), g_scan_snapshot.access_points.size());
  for (size_t index = 0; index < g_scan_snapshot.access_point_count; ++index) {
    auto &target = g_scan_snapshot.access_points[index];
    std::snprintf(target.ssid.data(), target.ssid.size(), "%s", records[index].ssid.c_str());
    target.rssi = records[index].rssi;
    target.secure = records[index].secure;
    target.authmode = records[index].authmode;
    target.channel = records[index].channel;
  }
  return {true, false, "wifi_scan_ready"};
}

bool start_or_join_scan(uint32_t &generation, std::string &error) {
  error.clear();
  if (!network_policy().station_enabled) {
    error = "network_station_disabled";
    return false;
  }
  if (!network_snapshot().service_started) {
    error = "network_service_not_started";
    return false;
  }
  {
    StaticLockGuard lock(g_scan_mutex);
    reconcile_locked();
    if (g_scan_snapshot.state == NetworkWifiScanState::Scanning) {
      generation = g_generation;
      return true;
    }
    if (++g_generation == 0) ++g_generation;
    generation = g_generation;
    g_scan_snapshot.state = NetworkWifiScanState::Scanning;
    g_scan_snapshot.error[0] = '\0';
    g_scan_snapshot.access_point_count = 0;
    g_scan_snapshot.access_points = {};
    ++g_scan_snapshot.sequence;
    g_scan_job = {};
    g_submitting = true;
  }
  NetworkJobHandle handle;
  uint32_t deadline = internal::system_monotonic_ms() + kScanQueueTimeoutMs;
  if (deadline == 0) deadline = 1;
  const NetworkJobDefinition job = {
      .owner_service_id = "network.wifi.scan",
      .kind = "wifi.scan",
      .priority = kNetworkPriorityInteractive,
      .maximum_attempts = 60,
      .deadline_ms = deadline,
      .runner = run_wifi_scan,
      .context = reinterpret_cast<void *>(static_cast<uintptr_t>(generation)),
      .retry_backoff_initial_ms = 250,
      .retry_backoff_maximum_ms = 250,
  };
  // Submission may immediately wake the runner, which needs g_scan_mutex.
  const bool submitted = submit_network_job(job, handle, error);
  {
    StaticLockGuard lock(g_scan_mutex);
    if (generation == g_generation) {
      g_submitting = false;
      g_scan_job = handle;
      if (!submitted) fail_locked(error.empty() ? "wifi_scan_queue_failed" : error.c_str());
      return submitted;
    }
  }
  if (submitted) {
    std::string ignored;
    (void)cancel_network_job(handle, ignored);
  }
  error = "wifi_scan_superseded";
  return false;
}

} // namespace

bool request_network_wifi_scan(std::string &error) {
  uint32_t generation;
  return start_or_join_scan(generation, error);
}

bool request_network_wifi_scan_and_wait(NetworkWifiScanSnapshot &snapshot,
                                       std::string &error, uint32_t timeout_ms) {
  snapshot = {};
  error.clear();
  if (current_task_owns_service_executor() || network_job_runner_context_active()) {
    error = "wifi_scan_wait_requires_external_caller";
    return false;
  }
  if (timeout_ms == 0 || timeout_ms > kScanQueueTimeoutMs) {
    error = "wifi_scan_invalid_timeout";
    return false;
  }
  uint32_t generation;
  const uint32_t started = internal::system_monotonic_ms();
  if (!start_or_join_scan(generation, error)) return false;
  for (;;) {
    {
      StaticLockGuard lock(g_scan_mutex);
      if (generation != g_generation) {
        error = "wifi_scan_superseded";
        return false;
      }
      reconcile_locked();
      if (g_scan_snapshot.state != NetworkWifiScanState::Scanning) {
        snapshot = g_scan_snapshot;
        error = snapshot.error.data();
        return snapshot.state == NetworkWifiScanState::Ready;
      }
    }
    const uint32_t elapsed = internal::system_monotonic_ms() - started;
    if (elapsed >= timeout_ms) {
      error = "wifi_scan_timeout";
      return false;
    }
    internal::system_delay_ms(std::min<uint32_t>(10, timeout_ms - elapsed));
  }
}

NetworkWifiScanSnapshot network_wifi_scan_snapshot() {
  StaticLockGuard lock(g_scan_mutex);
  reconcile_locked();
  return g_scan_snapshot;
}

void network_wifi_scan_snapshot_into(NetworkWifiScanSnapshot &snapshot) {
  StaticLockGuard lock(g_scan_mutex);
  reconcile_locked();
  snapshot = g_scan_snapshot;
}

void reset_network_wifi_scan_internal() {
  StaticLockGuard lock(g_scan_mutex);
  if (++g_generation == 0) ++g_generation;
  g_scan_snapshot = {};
  g_scan_job = {};
  g_submitting = false;
}

} // namespace lunaredu::lunaros
