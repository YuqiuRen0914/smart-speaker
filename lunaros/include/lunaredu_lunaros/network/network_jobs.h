#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace lunaredu::lunaros {

inline constexpr size_t kNetworkJobCapacity = 16;
inline constexpr size_t kNetworkOtaReservedSlots = 2;

// Priority is intentionally coarse. Callers choose a service class; the
// scheduler adds bounded ageing so a busy interactive service cannot starve
// older background work forever.
inline constexpr uint8_t kNetworkPriorityMaintenance = 32;
inline constexpr uint8_t kNetworkPriorityBackground = 80;
inline constexpr uint8_t kNetworkPriorityControl = 144;
inline constexpr uint8_t kNetworkPriorityInteractive = 208;
inline constexpr uint8_t kNetworkPriorityCritical = 240;

struct NetworkJobPolicy {
  size_t maximum_active_jobs = 12;
  size_t maximum_jobs_per_owner = 4;
  uint8_t ageing_boost_per_dispatch = 8;
  uint8_t maximum_ageing_boost = 255;
  uint32_t default_retry_backoff_ms = 1000;
  uint32_t maximum_retry_backoff_ms = 30000;
};

enum class NetworkJobState : uint8_t {
  Free,
  Queued,
  Running,
  Succeeded,
  Failed,
  Cancelled,
};

// Ordinary work may share the single blocking-I/O executor. OTA work is
// admitted only inside an explicit product-level maintenance window.
enum class NetworkJobClass : uint8_t {
  Normal,
  Ota,
};

enum class NetworkMaintenanceState : uint8_t {
  Normal,
  OtaPending,
  OtaExclusive,
};

struct NetworkJobHandle {
  // Unique for the lifetime of the scheduler, including service restarts.
  uint32_t id = 0;

  explicit operator bool() const { return id != 0; }
};

struct NetworkJobRunResult {
  bool succeeded = false;
  bool retryable = false;
  std::string detail;
};

using NetworkJobRunner = NetworkJobRunResult (*)(NetworkJobHandle handle,
                                                  void *context);
using NetworkJobWakeHandler = void (*)(void *context);

struct NetworkJobDefinition {
  const char *owner_service_id = nullptr;
  const char *kind = nullptr;
  uint8_t priority = 0;
  uint8_t maximum_attempts = 1;
  // Absolute dispatch/retry deadline. Does not undo a successful side effect
  // or preempt a running callback; runners own I/O timeouts and cancellation.
  uint32_t deadline_ms = 0;
  NetworkJobRunner runner = nullptr;
  void *context = nullptr;
  // Fire-and-forget work keeps this false so completed records cannot exhaust
  // the fixed registry. Diagnostics may retain and explicitly release it.
  bool retain_terminal = false;
  // Zero selects the corresponding scheduler policy default. Retries use
  // exponential backoff and are capped by retry_backoff_maximum_ms.
  uint32_t retry_backoff_initial_ms = 0;
  uint32_t retry_backoff_maximum_ms = 0;
  NetworkJobClass job_class = NetworkJobClass::Normal;
};

struct NetworkJobSnapshot {
  NetworkJobHandle handle;
  NetworkJobState state = NetworkJobState::Free;
  std::string owner_service_id;
  std::string kind;
  uint8_t priority = 0;
  uint8_t attempts = 0;
  uint8_t maximum_attempts = 0;
  uint32_t deadline_ms = 0;
  uint32_t sequence = 0;
  uint32_t ready_at_ms = 0;
  uint32_t dispatch_wait_count = 0;
  bool cancellation_requested = false;
  std::string detail;
  NetworkJobClass job_class = NetworkJobClass::Normal;
};

struct NetworkJobMetrics {
  uint32_t submitted = 0;
  uint32_t completed = 0;
  uint32_t retried = 0;
  uint32_t cancelled = 0;
  uint32_t deadline_exceeded = 0;
  uint32_t rejected_invalid = 0;
  uint32_t rejected_capacity = 0;
  uint32_t rejected_owner_limit = 0;
  uint32_t rejected_maintenance = 0;
};

struct NetworkJobManagerSnapshot {
  NetworkJobPolicy policy;
  NetworkJobMetrics metrics;
  NetworkMaintenanceState maintenance_state = NetworkMaintenanceState::Normal;
  size_t record_count = 0;
  size_t active_count = 0;
  size_t queued_count = 0;
  size_t running_count = 0;
  size_t terminal_count = 0;
  std::array<NetworkJobSnapshot, kNetworkJobCapacity> jobs;
};

// The manager implementation and its lock/storage layout are private to the
// Network module. Consumers submit typed jobs through this bounded facade.
bool submit_network_job(const NetworkJobDefinition &definition,
                        NetworkJobHandle &handle, std::string &error);
void set_network_job_wake_handler(NetworkJobWakeHandler handler,
                                  void *context);
bool cancel_network_job(NetworkJobHandle handle, std::string &error);
bool network_job_cancellation_requested(NetworkJobHandle handle);
// Allocation-free check for owners retaining a handle across retries.
bool network_job_active(NetworkJobHandle handle);
// Begin blocks new ordinary submissions and asks the currently running
// ordinary job to cancel cooperatively. Queued ordinary work is deferred and
// resumes if OTA fails and the maintenance window is released.
bool begin_network_ota_maintenance(std::string &error);
// The product calls activate only after motion, scripts and assistant services
// have quiesced. OTA-class jobs cannot run before this transition.
bool activate_network_ota_maintenance(std::string &error);
void end_network_ota_maintenance();
NetworkMaintenanceState network_maintenance_state();
bool run_next_network_job(uint32_t now_ms);
uint32_t next_network_job_wake_delay_ms(uint32_t now_ms);
bool release_network_job(NetworkJobHandle handle, std::string &error);
NetworkJobManagerSnapshot network_job_manager_snapshot();
// Synchronous transports use this guard to reject calls that bypass netio.
bool network_job_runner_context_active();

} // namespace lunaredu::lunaros
