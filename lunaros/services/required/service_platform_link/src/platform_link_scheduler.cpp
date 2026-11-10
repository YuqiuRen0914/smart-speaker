#include "platform_link_scheduler.h"
#include "platform_link_scheduler_job.h"

#include <atomic>
#include <mutex>
#include <string>
#include <sys/time.h>

#include "esp_heap_caps.h"

#include <esp_log.h>
#include "lunaros_network.h"
#include "lunaros_network_jobs.h"
#include "configuration/device_config.h"
#include "platform_link_http.h"
#include "platform_link_operations.h"
#include "platform_link_pairing_cycle.h"
#include "platform_link_state.h"
#include "platform_link_tls_policy.h"
#include "kernel/retry_schedule.h"
#include "platform/system.h"

namespace lunaredu::lunaros::platform_link {
namespace {

constexpr const char *kTag = "platform_link";
constexpr uint32_t kPresenceIntervalMs = 10000;
constexpr uint32_t kPresenceRetryIntervalMs = 5000;
constexpr uint32_t kCommandPollIntervalMs = 5000;
constexpr uint32_t kDiscoveryPublishIntervalMs = 2500;
constexpr uint32_t kPairingPollIntervalMs = 3000;
// TLS certificate verification needs a contiguous internal-RAM scratch area.
// Retrying every few seconds while audio/MQTT is active only creates more
// pressure and can starve the realtime audio/render tasks.
constexpr uint32_t kRetryIntervalMs = 30000;

std::atomic<bool> g_pairing_requested{false};
std::atomic<bool> g_open_claim_enrollment_requested{false};
std::atomic<bool> g_open_claim_enrollment_blocked{false};
std::atomic<uint32_t> g_last_open_claim_ms{0};
std::atomic<bool> g_pairing_cancel_requested{false};
std::atomic<uint32_t> g_pairing_cancel_retry_at{0};
std::atomic<bool> g_pairing_cancel_retry_scheduled{false};
std::atomic<bool> g_unbind_requested{false};
std::atomic<PlatformLinkUnbindState> g_unbind_state{PlatformLinkUnbindState::Idle};
std::atomic<uint32_t> g_discovery_requested_duration_ms{0};
std::mutex g_platform_link_job_mutex;
PlatformLinkJobContext g_platform_link_job;
RetrySchedule g_background_retry{5000, 30000}; // Protected by job mutex.
bool g_network_was_connected = false; // Network owner only.
std::atomic<uint32_t> g_last_presence_ms{0};
uint32_t g_last_binding_retry_ms = 0;
std::atomic<uint32_t> g_presence_interval_ms{kPresenceIntervalMs};
std::atomic<uint32_t> g_last_command_poll_ms{0};
std::atomic<uint32_t> g_last_discovery_publish_ms{0};
std::atomic<uint32_t> g_last_pairing_poll_ms{0};
std::atomic<uint32_t> g_pairing_refresh_due_ms{0};
int64_t g_observed_pairing_expiry_ms = 0;
uint32_t g_last_auto_pairing_ms = 0;
uint32_t g_last_pairing_refresh_request_ms = 0;
std::atomic<bool> g_firmware_update_requested{false};
std::atomic<bool> g_firmware_check_requested{false};
std::atomic<PlatformLinkFirmwareCheckState> g_firmware_check_state{PlatformLinkFirmwareCheckState::Idle};
// 0 = none, 1 = production, 2 = development.
std::atomic<uint8_t> g_environment_switch_requested{0};
std::string g_requested_origin; // Protected by g_platform_link_job_mutex.
std::atomic<bool> g_environment_restart_pending{false};
std::atomic<bool> g_environment_restart_requested{false};

const char *operation_kind(PlatformLinkOperation operation) {
  switch (operation) {
  case PlatformLinkOperation::PersistBinding: return "binding_persist";
  case PlatformLinkOperation::OpenClaimEnrollment: return "open_claim_enroll";
  case PlatformLinkOperation::CancelPairing: return "pairing_cancel";
  case PlatformLinkOperation::Unbind: return "binding_remove";
  case PlatformLinkOperation::CreatePairing: return "pairing_create";
  case PlatformLinkOperation::Presence: return "presence";
  case PlatformLinkOperation::FirmwareUpdate: return "firmware_update";
  case PlatformLinkOperation::FirmwareCheck: return "firmware_check";
  case PlatformLinkOperation::Commands: return "commands_poll";
  case PlatformLinkOperation::PollPairing: return "pairing_poll";
  case PlatformLinkOperation::StartDiscovery: return "discovery_start";
  case PlatformLinkOperation::Discovery: return "discovery_publish";
  case PlatformLinkOperation::None: return "platform_link_operation";
  }
  return "platform_link_operation";
}

NetworkJobRunResult run_platform_link_operation(NetworkJobHandle handle, void *) {
  PlatformLinkOperation operation = PlatformLinkOperation::None;
  uint32_t duration_ms = 0;
  {
    std::lock_guard<std::mutex> lock(g_platform_link_job_mutex);
    if (g_platform_link_job.handle.id != handle.id) {
      return {false, false, "platform_link_job_stale"};
    }
    operation = g_platform_link_job.operation;
    duration_ms = g_platform_link_job.duration_ms;
    g_platform_link_job.running = true;
  }

  bool succeeded = false;
  if (operation == PlatformLinkOperation::PersistBinding ||
      (operation == PlatformLinkOperation::Unbind &&
       pairing_state() == PlatformLinkBindingState::Unbound && !binding_revocation_requested())) {
    const auto unbind = g_unbind_state.load();
    const bool finishes_unbind = operation == PlatformLinkOperation::Unbind ||
        (pairing_state() == PlatformLinkBindingState::Unbound &&
         (unbind == PlatformLinkUnbindState::Failed || unbind == PlatformLinkUnbindState::Pending));
    std::string error;
    succeeded = operation == PlatformLinkOperation::Unbind ? unbind_device()
                                                          : retry_binding_persistence(error);
    if (finishes_unbind)
      g_unbind_state.store(succeeded ? PlatformLinkUnbindState::Succeeded : PlatformLinkUnbindState::Failed);
    std::lock_guard<std::mutex> lock(g_platform_link_job_mutex);
    if (g_platform_link_job.handle.id == handle.id) g_platform_link_job = {};
    return {succeeded, false, succeeded ? "binding_committed" : "binding_commit_failed"};
  }
  struct Completion {
    NetworkJobHandle handle;
    PlatformLinkOperation operation;
    const bool &succeeded;
    ~Completion() {
      std::lock_guard<std::mutex> lock(g_platform_link_job_mutex);
      if (g_platform_link_job.handle.id != handle.id) return;
      // Discovery preparation is local; all other operations in this scope
      // attempt cloud I/O. A single endpoint must not alternate failing jobs
      // to evade their individual periodic intervals.
      if (operation != PlatformLinkOperation::StartDiscovery) {
        if (succeeded) g_background_retry.reset();
        else g_background_retry.failed(monotonic_ms());
      }
      g_platform_link_job = {};
    }
  } finish{handle, operation, succeeded};
  // Report completion independently of presence, which can replace the pairing
  // snapshot immediately after failure. Early admission failures also finish.
  struct UnbindCompletion {
    bool active;
    const bool &succeeded;
    ~UnbindCompletion() {
      if (active && pairing_state() != PlatformLinkBindingState::Unbound) g_unbind_state.store(succeeded ? PlatformLinkUnbindState::Succeeded
                                               : PlatformLinkUnbindState::Failed);
    }
  } completion{operation == PlatformLinkOperation::Unbind, succeeded};
  struct FirmwareCheckCompletion {
    bool active;
    const bool &succeeded;
    ~FirmwareCheckCompletion() {
      if (active) g_firmware_check_state.store(succeeded ? PlatformLinkFirmwareCheckState::Succeeded
                                                       : PlatformLinkFirmwareCheckState::Failed);
    }
  } firmware_check_completion{operation == PlatformLinkOperation::FirmwareCheck, succeeded};
  // Do this admission check before suspending Assistant.  When the available
  // internal heap is already too fragmented for a TLS handshake, entering
  // and leaving cloud maintenance only tears down and recreates MQTT, which
  // can starve the assistant and turn one failed presence attempt into a
  // reconnect storm.
  const size_t internal_largest =
      heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
  const auto maintenance_gate = runtime_config().cloud_maintenance_gate;
  const PlatformLinkPairingSnapshot pairing = pairing_snapshot();
  // Interactive binding operations are allowed through this admission gate.
  // The cloud maintenance callback below suspends Assistant and releases the
  // wake-word frontend before the TLS request.  Also allow the first presence
  // reconciliation while the local state is Connecting (for example after a
  // reboot following an unbind); once the server confirms Bound/Unbound,
  // periodic presence keeps the original low-memory backoff behavior.
  const bool allow_interactive_memory_recovery =
      operation == PlatformLinkOperation::OpenClaimEnrollment ||
      operation == PlatformLinkOperation::CancelPairing ||
      operation == PlatformLinkOperation::Unbind ||
      operation == PlatformLinkOperation::CreatePairing ||
      operation == PlatformLinkOperation::PollPairing ||
      operation == PlatformLinkOperation::FirmwareUpdate ||
      operation == PlatformLinkOperation::FirmwareCheck ||
      (operation == PlatformLinkOperation::Presence &&
       pairing.state == PlatformLinkBindingState::Connecting);
  // Every cloud request needs TLS memory, including development endpoints.
  // Opening a LAN discovery window only sets local state and synchronizes
  // the clock; no HTTPS request occurs until Discovery publishes it.
  const PlatformLinkTlsAdmission tls_admission = platform_link_tls_admission(
      operation != PlatformLinkOperation::StartDiscovery, internal_largest,
      allow_interactive_memory_recovery);
  if (tls_admission == PlatformLinkTlsAdmission::Defer) {
    const uint32_t now = monotonic_ms();
    switch (operation) {
    case PlatformLinkOperation::Presence:
      g_last_presence_ms.store(now);
      g_presence_interval_ms.store(kPresenceRetryIntervalMs);
      break;
    case PlatformLinkOperation::Commands:
      g_last_command_poll_ms.store(now);
      break;
    case PlatformLinkOperation::PollPairing:
      g_last_pairing_poll_ms.store(now);
      break;
    case PlatformLinkOperation::Discovery:
      g_last_discovery_publish_ms.store(now);
      break;
    default:
      break;
    }
    ESP_LOGW(kTag, "deferring cloud TLS: internal largest block=%u < %u",
         static_cast<unsigned>(internal_largest),
         static_cast<unsigned>(kNetworkMinimumTlsBlockBytes));
    return {false, true, "cloud_tls_internal_memory_low"};
  }

  bool maintenance_entered = false;
  // An admitted request already has the measured TLS scratch block, so it
  // must not tear down Assistant merely to perform routine command, presence,
  // pairing, or discovery traffic. The maintenance gate is reserved for an
  // interactive request that must recover from a genuinely low-memory state.
  if (tls_admission == PlatformLinkTlsAdmission::Maintenance &&
      maintenance_gate != nullptr) {
    maintenance_entered = maintenance_gate(true);
    if (!maintenance_entered) {
      // Entry was rejected (for example because Assistant is listening or
      // speaking). Do not emit an unmatched leave call: another foreground
      // owner or maintenance job may legitimately hold the suspension, and
      // ending it here races the first assistant reply back into reconnect.
      if (operation == PlatformLinkOperation::Presence) {
        g_last_presence_ms.store(monotonic_ms());
        g_presence_interval_ms.store(kPresenceRetryIntervalMs);
      }
      return {false, true, "cloud_maintenance_not_ready"};
    }
  }
  switch (operation) {
  case PlatformLinkOperation::OpenClaimEnrollment:
    {
    int http_status=-1;
    succeeded = enroll_open_claim(&http_status);
    if(http_status==400 || http_status==403 || http_status==409)
      g_open_claim_enrollment_blocked.store(true);
    // A transient enrollment failure must not starve authenticated presence.
    // Retry enrollment only after presence explicitly rejects this identity.
    g_open_claim_enrollment_requested.store(false);
    g_last_presence_ms.store(0);
    }
    break;
  case PlatformLinkOperation::CancelPairing:
    succeeded = !account_identity_ready() || cancel_pairing_session();
    if (succeeded) {
      if (!account_identity_ready()) update_pairing_snapshot(PlatformLinkBindingState::Unbound);
      g_pairing_cancel_requested.store(false);
      g_pairing_cancel_retry_at.store(0);
      g_pairing_cancel_retry_scheduled.store(false);
    } else {
      g_pairing_cancel_retry_at.store(monotonic_ms() + 5000);
      g_pairing_cancel_retry_scheduled.store(true);
    }
    g_last_pairing_poll_ms.store(0);
    break;
  case PlatformLinkOperation::Unbind:
    succeeded = unbind_device();
    g_last_presence_ms.store(0);
    invalidate_discovery_announcement();
    g_last_command_poll_ms.store(0);
    break;
  case PlatformLinkOperation::CreatePairing:
    succeeded = create_pairing_session();
    g_last_pairing_poll_ms.store(monotonic_ms());
    break;
  case PlatformLinkOperation::Presence:
    {
    int http_status=-1;
    succeeded = publish_presence(&http_status);
    if(succeeded) {
      g_open_claim_enrollment_requested.store(false);
      g_open_claim_enrollment_blocked.store(false);
    } else if(http_status==401 && runtime_config().open_claim && !g_open_claim_enrollment_blocked.load()) {
      g_open_claim_enrollment_requested.store(true);
    }
    }
    g_last_presence_ms.store(monotonic_ms());
    g_presence_interval_ms.store(succeeded ? kPresenceIntervalMs : kPresenceRetryIntervalMs);
    if (!succeeded) {
      ESP_LOGW(kTag, "presence publish failed; retrying in %u ms",
           static_cast<unsigned>(kPresenceRetryIntervalMs));
    }
    break;
  case PlatformLinkOperation::FirmwareCheck:
    succeeded = refresh_available_firmware();
    break;
  case PlatformLinkOperation::FirmwareUpdate:
    succeeded = refresh_and_apply_firmware_update();
    break;
  case PlatformLinkOperation::Commands:
    succeeded = poll_commands();
    g_last_command_poll_ms.store(monotonic_ms());
    break;
  case PlatformLinkOperation::PollPairing:
    succeeded = poll_pairing_session();
    g_last_pairing_poll_ms.store(monotonic_ms());
    break;
  case PlatformLinkOperation::StartDiscovery:
    succeeded = begin_discovery(duration_ms);
    // Preserve immediate publishing when a new discovery window is opened.
    g_last_discovery_publish_ms.store(0);
    break;
  case PlatformLinkOperation::Discovery:
    succeeded = publish_discovery_announcement();
    g_last_discovery_publish_ms.store(monotonic_ms());
    break;
  case PlatformLinkOperation::PersistBinding:
  case PlatformLinkOperation::None:
    break;
  }

  if (maintenance_entered) {
    maintenance_gate(false);
  }

  return {succeeded, false,
          succeeded ? "platform_link_operation_complete"
                    : "platform_link_operation_failed"};
}

bool submit_platform_link_operation(PlatformLinkOperation operation,
                                  uint8_t priority,
                                  uint32_t duration_ms = 0,
                                  bool yieldable_setup = false) {
  std::lock_guard<std::mutex> lock(g_platform_link_job_mutex);
  if (g_platform_link_job.handle || environment_switch_pending()) {
    return false;
  }
  std::string error;
  NetworkJobHandle handle;
  NetworkJobDefinition definition{
      "cloud.platform_link", operation_kind(operation), priority, 1, 0,
      run_platform_link_operation, nullptr, false};
  definition.yieldable_transport_setup = yieldable_setup;
  if (operation == PlatformLinkOperation::FirmwareUpdate) {
    definition.job_class = NetworkJobClass::Ota;
  }
  if (!submit_network_job(
          definition, handle, error)) {
    ESP_LOGW(kTag, "failed to queue platform link operation: %s", error.c_str());
    return false;
  }
  g_platform_link_job.operation = operation;
  g_platform_link_job.handle = handle;
  g_platform_link_job.duration_ms = duration_ms;
  return true;
}

bool submit_automatic_operation(PlatformLinkOperation operation, uint8_t priority,
                                uint32_t duration_ms = 0) {
  // Automatic work retains its existing completion-based retry ownership.
  // Explicit user operations stay non-yielding; priority is independent.
  return submit_platform_link_operation(operation, priority, duration_ms, true);
}

bool platform_link_job_active() {
  std::lock_guard<std::mutex> lock(g_platform_link_job_mutex);
  // A queued job can be cancelled before its runner executes. Reconcile the
  // handle with the job owner instead of waiting forever for that callback.
  if (g_platform_link_job.handle && !network_job_active(g_platform_link_job.handle))
    g_platform_link_job = {};
  return static_cast<bool>(g_platform_link_job.handle);
}

bool apply_environment_switch_if_requested() {
  std::unique_lock<std::mutex> lock(g_platform_link_job_mutex);
  const uint8_t requested = g_environment_switch_requested.load();
  if (requested == 0) {
    return false;
  }
  const bool production = requested == 1;
  const char *environment = production ? "production" : "development";
  const auto current = pairing_snapshot();
  if (g_platform_link_job.handle || current.state == PlatformLinkBindingState::Pairing) {
    g_environment_switch_requested.store(0);
    return true;
  }
  // A new server never inherits the previous server's credential. Persist the
  // endpoint and fresh identity together so interrupted writes cannot mix them.
  const std::string credential = create_pairing_nonce();
  if (!prepare_environment_cache_reset() ||
      !save_device_cloud_profile(environment, credential.c_str(), g_requested_origin)) {
    g_environment_switch_requested.store(0);
    lock.unlock();
    update_pairing_snapshot(PlatformLinkBindingState::Error, {}, {}, 0,
                            "environment_save_failed");
    return true;
  }
  g_environment_restart_pending.store(true);
  g_environment_switch_requested.store(0);
  lock.unlock(); // Product observers must never run under the job mutex.
  update_pairing_snapshot(PlatformLinkBindingState::Unbound, {}, {}, 0, {}, 0);
  update_platform_connection(PlatformLinkPlatformConnectionState::Disconnected);
  ESP_LOGI(kTag,
       "platform environment changed environment=%s url=%s; awaiting restart",
       environment, platform_base_url().c_str());
  return true;
}

} // namespace

void tick_boot_authority_recovery() {
  if (platform_link_job_active() || g_environment_restart_pending.load()) return;
  if (binding_persistence_pending()) {
    const uint32_t now = monotonic_ms();
    if (g_last_binding_retry_ms == 0 || now - g_last_binding_retry_ms >= 5000U) {
      if (submit_platform_link_operation(PlatformLinkOperation::PersistBinding,kNetworkPriorityInteractive))
        g_last_binding_retry_ms = now;
    }
    return;
  }
  if (!network_connected() || !account_identity_ready()) return;
  const uint32_t now = monotonic_ms();
  if (runtime_config().open_claim && g_open_claim_enrollment_requested.load()) {
    const uint32_t last = g_last_open_claim_ms.load();
    if (!last || now - last >= kRetryIntervalMs) {
      g_last_open_claim_ms.store(now);
      (void)submit_automatic_operation(PlatformLinkOperation::OpenClaimEnrollment,kNetworkPriorityInteractive);
      return;
    }
  }
  const uint32_t last = g_last_presence_ms.load();
  if (!last || now - last >= kPresenceRetryIntervalMs) {
    if (!submit_automatic_operation(PlatformLinkOperation::Presence,kNetworkPriorityInteractive))
      g_last_presence_ms.store(now);
  }
}

ConnectionRetrySnapshot connection_retry_snapshot() {
  std::lock_guard<std::mutex> lock(g_platform_link_job_mutex);
  ConnectionRetrySnapshot result;
  result.wait_ms = g_background_retry.remaining_ms(monotonic_ms());
  if (g_platform_link_job.handle && network_job_active(g_platform_link_job.handle) &&
      g_platform_link_job.operation != PlatformLinkOperation::PersistBinding &&
      g_platform_link_job.operation != PlatformLinkOperation::StartDiscovery) {
    result.running = g_platform_link_job.running;
    result.queued = !result.running;
  }
  return result;
}

void tick_scheduler() {
  if (!platform_link_job_active() && apply_environment_switch_if_requested()) {
    return;
  }
  if (g_environment_restart_pending.load()) {
    if (g_environment_restart_requested.exchange(false)) {
      ESP_LOGI(kTag, "restarting to apply platform environment=%s",
           platform_is_production() ? "production" : "development");
      system_restart_after_delay(200);
    }
    // The persisted environment is now different from the active runtime.
    // Do not start another PlatformLink request until reboot rebuilds every
    // transport and private-resource authority from one coherent profile.
    return;
  }
  if (platform_link_job_active()) return;
  if (binding_persistence_pending()) {
    const uint32_t now = monotonic_ms();
    const bool user_retry = pairing_state() == PlatformLinkBindingState::Unbound &&
                            g_unbind_requested.exchange(false);
    if (user_retry || g_last_binding_retry_ms == 0 || now - g_last_binding_retry_ms >= 5000U) {
      if (submit_platform_link_operation(user_retry ? PlatformLinkOperation::Unbind : PlatformLinkOperation::PersistBinding,
                                        kNetworkPriorityInteractive)) g_last_binding_retry_ms = now;
      else if (user_retry) g_unbind_requested.store(true);
    }
    return;
  }
  // A user retry can arrive while an automatic commit finishes. Complete that
  // queued local request even if the device went offline in the meantime.
  if (pairing_state() == PlatformLinkBindingState::Unbound && !binding_revocation_requested() && g_unbind_requested.exchange(false)) {
    if (!submit_platform_link_operation(PlatformLinkOperation::Unbind, kNetworkPriorityInteractive))
      g_unbind_requested.store(true);
    return;
  }
  if (!network_connected()) {
    g_network_was_connected = false;
    update_platform_connection(PlatformLinkPlatformConnectionState::Disconnected);

    return;
  }
  if (!g_network_was_connected) {
    std::lock_guard<std::mutex> lock(g_platform_link_job_mutex);
    g_network_was_connected = true;
    g_background_retry.reset(); // A restored transport deserves a fresh attempt.
    g_last_presence_ms.store(0);
    invalidate_discovery_announcement();
  }
  if (platform_link_job_active()) {
    return;
  }
  if (g_pairing_cancel_requested.load()) {
    const auto retry_at = g_pairing_cancel_retry_at.load();
    if (g_pairing_cancel_retry_scheduled.load() && static_cast<int32_t>(monotonic_ms() - retry_at) < 0) return;
    g_pairing_cancel_retry_at.store(monotonic_ms() + 5000);
    g_pairing_cancel_retry_scheduled.store(true);
    if (!submit_platform_link_operation(PlatformLinkOperation::CancelPairing,
                                      kNetworkPriorityInteractive)) {
      g_pairing_cancel_requested.store(true);
    }
    return;
  }
  if (g_unbind_requested.exchange(false)) {
    if (!submit_platform_link_operation(PlatformLinkOperation::Unbind,
                                      kNetworkPriorityInteractive)) {
      g_unbind_requested.store(true);
    }
    return;
  }
  if (binding_revocation_requested()) {
    std::lock_guard<std::mutex> lock(g_platform_link_job_mutex);
    if (!g_background_retry.ready(monotonic_ms())) return;
  }
  if (binding_revocation_requested()) {
    (void)submit_automatic_operation(PlatformLinkOperation::Unbind, kNetworkPriorityInteractive);
    return;
  }
  if (g_pairing_requested.exchange(false)) {
    if (!submit_platform_link_operation(PlatformLinkOperation::CreatePairing,
                                      kNetworkPriorityInteractive)) {
      // Interactive pairing must not be lost merely because the shared
      // network queue was briefly full. Keep the bounded scheduler as the
      // single owner and retry on its next tick.
      g_pairing_requested.store(true);
    }
    return;
  }
  const uint32_t discovery_duration_ms =
      g_discovery_requested_duration_ms.exchange(0);
  if (discovery_duration_ms != 0) {
    if (!submit_platform_link_operation(PlatformLinkOperation::StartDiscovery,
                                  kNetworkPriorityInteractive,
                                  discovery_duration_ms)) {
      g_discovery_requested_duration_ms.store(discovery_duration_ms);
    }
    return;
  }
  // Explicit user requests remain responsive during automatic cooldown.
  if (g_firmware_update_requested.exchange(false)) {
    if (!submit_platform_link_operation(PlatformLinkOperation::FirmwareUpdate,
                                      kNetworkPriorityCritical))
      g_firmware_update_requested.store(true);
    return;
  }
  if (g_firmware_check_requested.exchange(false)) {
    if (!submit_platform_link_operation(PlatformLinkOperation::FirmwareCheck,
                                      kNetworkPriorityInteractive))
      g_firmware_check_requested.store(true);
    return;
  }
  {
    std::lock_guard<std::mutex> lock(g_platform_link_job_mutex);
    if (!g_background_retry.ready(monotonic_ms())) return;
  }
  if (!account_identity_ready()) {
    if (runtime_config().auto_pairing) {
      const uint32_t now = monotonic_ms();
      if (g_last_auto_pairing_ms == 0 ||
          now - g_last_auto_pairing_ms >= 5000U) {
        g_last_auto_pairing_ms = now;
        (void)request_platform_link_pairing();
      }
      return;
    }
    if (pairing_state() != PlatformLinkBindingState::Error) {
      update_pairing_snapshot(PlatformLinkBindingState::Unbound);
    }

    return;
  }

  if (runtime_config().open_claim && g_open_claim_enrollment_requested.load()) {
    const uint32_t now = monotonic_ms();
    const uint32_t last = g_last_open_claim_ms.load();
    // Failed enrollment must not occupy the network I/O worker every tick.
    if (last == 0 || now - last >= kRetryIntervalMs) {
      g_last_open_claim_ms.store(now);
      (void)submit_automatic_operation(PlatformLinkOperation::OpenClaimEnrollment,
                                        kNetworkPriorityInteractive);
      return;
    }
    // Enrollment's own cooldown never suppresses ordinary presence recovery.
  }

  const uint32_t now = monotonic_ms();
  if (runtime_config().auto_pairing) {
    const auto binding = pairing_snapshot();
    if (binding.state == PlatformLinkBindingState::Pairing &&
        !binding.code.empty()) {
      if (binding.expires_at_ms != g_observed_pairing_expiry_ms) {
        g_observed_pairing_expiry_ms = binding.expires_at_ms;
        timeval wall_time{};
        gettimeofday(&wall_time, nullptr);
        const int64_t wall_ms = static_cast<int64_t>(wall_time.tv_sec) * 1000 +
                                wall_time.tv_usec / 1000;
        const uint32_t duration = pairing_rotation_delay_ms(
            binding.expires_at_ms, wall_ms);
        g_pairing_refresh_due_ms.store(now + duration);
        g_last_pairing_refresh_request_ms = 0;
      }
      const uint32_t due = g_pairing_refresh_due_ms.load();
      if (static_cast<int32_t>(now - due) >= 0 &&
          (g_last_pairing_refresh_request_ms == 0 ||
           now - g_last_pairing_refresh_request_ms >= 5000U)) {
        g_last_pairing_refresh_request_ms = now;
        if (submit_automatic_operation(PlatformLinkOperation::CreatePairing,
                                         kNetworkPriorityInteractive)) return;
      }
    } else {
      g_observed_pairing_expiry_ms = 0;
      g_pairing_refresh_due_ms.store(0);
      if ((binding.state == PlatformLinkBindingState::Unbound ||
           binding.state == PlatformLinkBindingState::Error) &&
          (g_last_auto_pairing_ms == 0 ||
           now - g_last_auto_pairing_ms >= 5000U)) {
        g_last_auto_pairing_ms = now;
        if (request_platform_link_pairing()) return;
      }
    }
  }
  const PlatformLinkBindingState state = pairing_state();
  // Binding clears discovery server-side; a future unbind must open and
  // publish a fresh local window rather than inherit an old acknowledgement.
  if (state == PlatformLinkBindingState::Bound && discovery_active()) end_discovery();
  if (runtime_config().open_claim &&
      state == PlatformLinkBindingState::Unbound &&
      !discovery_active()) {
    (void)submit_automatic_operation(PlatformLinkOperation::StartDiscovery,
                                       kNetworkPriorityInteractive,
                                       kMaximumDiscoveryDurationMs);
    return;
  }
  const uint32_t last_presence = g_last_presence_ms.load();
  const uint32_t presence_interval = g_presence_interval_ms.load();
  if (last_presence == 0 || now - last_presence >= presence_interval) {
    if (!submit_automatic_operation(PlatformLinkOperation::Presence,
                                      kNetworkPriorityControl)) {
      g_last_presence_ms.store(now);
      g_presence_interval_ms.store(kPresenceRetryIntervalMs);
    }
    return;
  }
  const uint32_t last_commands = g_last_command_poll_ms.load();
  if (runtime_config().remote_commands_enabled &&
      state == PlatformLinkBindingState::Bound &&
      !binding_revocation_requested() && !binding_persistence_pending() &&
      (last_commands == 0 || now - last_commands >= kCommandPollIntervalMs)) {
    (void)submit_automatic_operation(PlatformLinkOperation::Commands,
                                   kNetworkPriorityControl);
    return;
  }
  const uint32_t last_pairing = g_last_pairing_poll_ms.load();
  if (state == PlatformLinkBindingState::Pairing &&
      (last_pairing == 0 || now - last_pairing >= kPairingPollIntervalMs)) {
    (void)submit_automatic_operation(PlatformLinkOperation::PollPairing,
                                   kNetworkPriorityControl);
    return;
  }
  const uint32_t last_discovery = g_last_discovery_publish_ms.load();
  if (discovery_active() &&
      (last_discovery == 0 ||
       now - last_discovery >= kDiscoveryPublishIntervalMs)) {
    // The interval bounds cheap metadata observation, not HTTPS publishing.
    // Only an unacknowledged lease/address/name/origin needs I/O admission.
    g_last_discovery_publish_ms.store(now);
    if (discovery_announcement_needed())
      (void)submit_automatic_operation(PlatformLinkOperation::Discovery,
                                     kNetworkPriorityBackground);
  }
}

bool reset_scheduler() {
  std::lock_guard<std::mutex> lock(g_platform_link_job_mutex);
  if (g_platform_link_job.handle) {
    std::string ignored;
    (void)cancel_network_job(g_platform_link_job.handle, ignored);
    if (network_job_active(g_platform_link_job.handle)) return false;
  }
  g_platform_link_job = {};
  g_background_retry.reset();
  g_network_was_connected = false;
  g_pairing_requested.store(false);
  g_open_claim_enrollment_requested.store(false);
  g_open_claim_enrollment_blocked.store(false);
  g_last_open_claim_ms.store(0);
  g_pairing_cancel_requested.store(false);
  g_pairing_cancel_retry_at.store(0);
  g_pairing_cancel_retry_scheduled.store(false);
  g_unbind_requested.store(false);
  g_unbind_state.store(PlatformLinkUnbindState::Idle);
  g_discovery_requested_duration_ms.store(0);
  g_last_presence_ms.store(0);
  g_presence_interval_ms.store(kPresenceIntervalMs);
  g_last_command_poll_ms.store(0);
  g_last_discovery_publish_ms.store(0);
  end_discovery();
  g_last_pairing_poll_ms.store(0);
  g_pairing_refresh_due_ms.store(0);
  g_observed_pairing_expiry_ms = 0;
  g_last_auto_pairing_ms = 0;
  g_last_binding_retry_ms = 0;
  g_last_pairing_refresh_request_ms = 0;
  g_firmware_update_requested.store(false);
  g_firmware_check_requested.store(false);
  g_firmware_check_state.store(PlatformLinkFirmwareCheckState::Idle);
  g_environment_switch_requested.store(0);
  g_requested_origin.clear();
  g_environment_restart_pending.store(false);
  g_environment_restart_requested.store(false);
  return true;
}

void request_open_claim_enrollment() {g_open_claim_enrollment_requested.store(true);}
void request_pairing() {
  g_pairing_requested.store(true);
}
bool pairing_request_pending() {
  std::lock_guard<std::mutex> lock(g_platform_link_job_mutex);
  return g_pairing_requested.load() ||
      (g_platform_link_job.handle && g_platform_link_job.operation==PlatformLinkOperation::CreatePairing);
}

bool pairing_cancel_pending() {
  std::lock_guard<std::mutex> lock(g_platform_link_job_mutex);
  return g_pairing_cancel_requested.load() || (g_platform_link_job.handle && g_platform_link_job.operation == PlatformLinkOperation::CancelPairing);
}
void request_pairing_cancel() {
  g_pairing_requested.store(false);
  if (!g_pairing_cancel_requested.load()) g_pairing_cancel_retry_scheduled.store(false);
  g_pairing_cancel_requested.store(true);
}

bool request_unbind() {
  auto state = g_unbind_state.load();
  do {
    if (state == PlatformLinkUnbindState::Pending) return false;
  } while (!g_unbind_state.compare_exchange_weak(state, PlatformLinkUnbindState::Pending));
  std::string error;
  if ((binding_persistence_pending() && !retry_binding_persistence(error)) ||
      (pairing_state() != PlatformLinkBindingState::Unbound && !prepare_binding_revocation(error))) {
    g_unbind_state.store(PlatformLinkUnbindState::Failed);
    return false;
  }
  g_pairing_requested.store(false);
  g_pairing_cancel_requested.store(false);
  g_unbind_state.store(PlatformLinkUnbindState::Succeeded);
  g_unbind_requested.store(true);
  return true;
}

PlatformLinkUnbindState unbind_state() {
  return g_unbind_state.load();
}

void request_discovery(uint32_t duration_ms) {
  g_discovery_requested_duration_ms.store(duration_ms);
}

void request_firmware_update() {
  g_firmware_update_requested.store(true);
}

bool firmware_update_pending() {
  std::lock_guard<std::mutex> lock(g_platform_link_job_mutex);
  return g_firmware_update_requested.load() ||
      g_platform_link_job.operation==PlatformLinkOperation::FirmwareUpdate;
}

bool request_firmware_check() {
  std::lock_guard<std::mutex> lock(g_platform_link_job_mutex);
  if (environment_switch_pending() || environment_restart_pending() ||
      g_firmware_update_requested.load() ||
      g_platform_link_job.operation == PlatformLinkOperation::FirmwareUpdate ||
      g_firmware_check_state.load() == PlatformLinkFirmwareCheckState::Pending) return false;
  g_firmware_check_state.store(PlatformLinkFirmwareCheckState::Pending);
  g_firmware_check_requested.store(true);
  return true;
}

PlatformLinkFirmwareCheckState firmware_check_state() {
  return g_firmware_check_state.load();
}

bool request_environment_switch(bool production, const std::string &origin) {
  std::lock_guard<std::mutex> lock(g_platform_link_job_mutex);
  if (environment_switch_pending() ||
      g_pairing_requested.load() || g_pairing_cancel_requested.load() ||
      g_unbind_requested.load() || g_unbind_state.load() == PlatformLinkUnbindState::Pending ||
      g_firmware_update_requested.load() ||
      g_firmware_check_state.load() == PlatformLinkFirmwareCheckState::Pending) {
    return false;
  }
  // Routine enrollment, discovery, or presence may be queued behind other
  // network work for a long time. Give the explicit local switch priority.
  // A running request is only marked for cancellation; the scheduler waits for
  // its runner to finish before persisting the new server and credential.
  if (g_platform_link_job.handle) {
    if (g_platform_link_job.operation != PlatformLinkOperation::OpenClaimEnrollment &&
        g_platform_link_job.operation != PlatformLinkOperation::StartDiscovery &&
        g_platform_link_job.operation != PlatformLinkOperation::Presence &&
        g_platform_link_job.operation != PlatformLinkOperation::Commands &&
        g_platform_link_job.operation != PlatformLinkOperation::Discovery) {
      return false;
    }
    std::string ignored;
    (void)cancel_network_job(g_platform_link_job.handle, ignored);
    if (!network_job_active(g_platform_link_job.handle)) {
      g_platform_link_job = {};
    }
  }
  g_requested_origin = origin;
  g_environment_switch_requested.store(production ? 1 : 2);
  return true;
}

bool environment_switch_pending() {
  return g_environment_switch_requested.load() != 0 || g_environment_restart_pending.load();
}

bool environment_restart_pending() {
  return g_environment_restart_pending.load();
}

bool request_environment_restart() {
  if (!g_environment_restart_pending.load()) {
    return false;
  }
  bool expected = false;
  return g_environment_restart_requested.compare_exchange_strong(expected,
                                                                  true);
}

} // namespace lunaredu::lunaros::platform_link
