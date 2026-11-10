#pragma once

#include "lunaros_network_job_policy.h"
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


enum class NetworkJobState : uint8_t {
  Free,
  Queued,
  Running,
  Succeeded,
  Failed,
  Cancelled,
};

// Optional coarse progress, with no endpoint, payload or credential data.
// TransportSetup includes name resolution/TCP/TLS; it cannot separate their
// SDK-internal blocking steps. This never changes dispatch or retry policy.
enum class NetworkJobPhase : uint8_t {
  Unspecified,
  TransportSetup,
  RequestSend,
  ResponseHeaders,
  ResponseBody,
  ResourceCleanup,
};

// Ordinary work may share the single blocking-I/O executor. OTA work is
// admitted only inside an explicit product-level maintenance window.
enum class NetworkJobClass : uint8_t {
  Normal,
  Ota,
  // Releases already owned transports/buffers. Must not create new network
  // sessions; runnable during pending maintenance so cleanup cannot deadlock.
  Retirement,
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

// Holds dispatch admission across a multi-step physical teardown and its
// owner callbacks. Already running I/O is allowed to finish; only Retirement
// jobs may start until every holder explicitly releases its window. No task,
// timer or heap allocation is created. A window must never span ordinary work.
struct NetworkRetirementWindow {
  uint32_t id = 0;
  explicit operator bool() const { return id != 0; }
};

// One bounded off-executor application-I/O owner. This is accounting for
// physical work, not a runner or a retirement window. OTA activation and manager
// reset wait for its release; entering OtaPending prevents new acquisitions.
struct NetworkExternalWork {
  uint64_t id = 0;
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
  // Absolute dispatch/retry and cooperative I/O deadline. Running transports
  // inherit its remaining budget; it neither preempts an SDK call nor undoes
  // a successful side effect. Runners still own cancellation and retirement.
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
  // Optional cooperative yield while establishing a transport, before any
  // request bytes may be sent. The runner retains normal failure/retry
  // ownership; this never authorizes cancellation of an in-flight side effect.
  bool yieldable_transport_setup = false;
  // Trusted domain owners may detach a separately accepted activity's work;
  // ordinary synchronous calls inherit their initiating operation budget.
  bool inherit_operation_context=true;
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
  bool yieldable_transport_setup = false;
  // Scheduler-clock timestamps; elapsed includes blocking I/O, not CPU time.
  NetworkJobPhase phase = NetworkJobPhase::Unspecified;
  uint32_t started_at_ms = 0;
  uint32_t last_run_elapsed_ms = 0;
  // Submission/retry to runner start, including backoff and admission pauses.
  // Measured on the runner's monotonic clock; this is not CPU time.
  uint32_t queued_at_ms = 0, last_queue_wait_ms = 0;
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
  uint32_t runner_calls = 0;
  uint64_t runner_elapsed_ms = 0;
  uint32_t maximum_run_elapsed_ms = 0;
  uint32_t maximum_queue_wait_ms = 0;
};

// Keeps attribution after fire-and-forget records are released, without a
// history buffer or allocations on each dispatch. Long labels are truncated.
struct NetworkJobLastRun {
  NetworkJobHandle handle;
  std::array<char, 64> owner_service_id{};
  std::array<char, 64> kind{};
  uint32_t elapsed_ms = 0;
  uint32_t queue_wait_ms = 0;
};

struct NetworkJobManagerSnapshot {
  NetworkJobPolicy policy;
  NetworkJobMetrics metrics;
  NetworkMaintenanceState maintenance_state = NetworkMaintenanceState::Normal;
  NetworkJobLastRun last_run;
  size_t record_count = 0;
  size_t active_count = 0;
  size_t queued_count = 0;
  size_t running_count = 0;
  size_t terminal_count = 0;
  size_t retirement_window_count = 0;
  bool external_normal_work = false;
  std::array<NetworkJobSnapshot, kNetworkJobCapacity> jobs;
};

// The manager implementation and its lock/storage layout are private to the
// Network module. Consumers submit typed jobs through this bounded facade.
bool submit_network_job(const NetworkJobDefinition &definition,
                        NetworkJobHandle &handle, std::string &error);
// Synchronous adapters share the existing I/O owner. Queued deadlines cancel
// before dispatch; running callbacks retain borrowed context until completion.
// Runners bound their own I/O. No new task and no replay/retry are introduced.
bool execute_network_operation(const NetworkJobDefinition &, std::string &error);

bool cancel_network_job(NetworkJobHandle handle, std::string &error);
bool network_job_cancellation_requested(NetworkJobHandle handle);
// Allocation-free check for owners retaining a handle across retries.
bool network_job_active(NetworkJobHandle handle);
bool acquire_network_retirement_window(NetworkRetirementWindow &window,
                                       std::string &error);
bool release_network_retirement_window(NetworkRetirementWindow &window,
                                       std::string &error);
bool acquire_network_external_work(NetworkExternalWork &work, std::string &error);
bool release_network_external_work(NetworkExternalWork &work, std::string &error);
// True only on a declared yielding runner's current I/O context when resource
// retirement/maintenance or cooperative cancellation asks setup to defer.
// Transports must stop consulting this once a request could have been sent.
bool network_job_runner_should_yield_transport_setup();
// Current runner's stop request, including after HTTP headers may have been
// sent. Observing this does not authorize replay or imply resource retirement.
bool network_job_runner_cancellation_requested();
// Remaining absolute job budget, capped by the caller's I/O limit. Nested
// runners inherit the earliest enclosing deadline; zero means already due.
uint32_t network_job_runner_remaining_timeout_ms(uint32_t ceiling_ms);
// Nested synchronous operations borrow an admitted normal runner, never an
// OTA/retirement runner or one whose admission has been withdrawn.
bool network_job_runner_accepts_normal_work();
// Observational only. Updates the current runner's record on a phase change;
// ignored outside a runner, without creating timers, strings or history.
void network_job_runner_set_phase(NetworkJobPhase phase);
// Begin blocks new ordinary submissions and asks the currently running
// ordinary job to cancel cooperatively. Queued ordinary work is deferred and
// resumes if OTA fails and the maintenance window is released.
bool begin_network_ota_maintenance(std::string &error);
// The product calls activate only after motion, scripts and assistant services
// have quiesced. OTA-class jobs cannot run before this transition.
bool activate_network_ota_maintenance(std::string &error);
void end_network_ota_maintenance();
NetworkMaintenanceState network_maintenance_state();
// Only the current netio owner may advance resource retirement while waiting
// for a product's OTA acknowledgement. Never executes normal or OTA jobs.
bool poll_network_ota_retirement(uint32_t now_ms);


bool release_network_job(NetworkJobHandle handle, std::string &error);
NetworkJobManagerSnapshot network_job_manager_snapshot();
// Allocation-free counters for bounded telemetry; does not copy job strings.
NetworkJobMetrics network_job_metrics();
// Synchronous transports use this guard to reject calls that bypass netio.


} // namespace lunaredu::lunaros
