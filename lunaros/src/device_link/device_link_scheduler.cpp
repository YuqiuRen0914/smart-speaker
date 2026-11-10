#include "device_link/device_link_scheduler.h"
#include "device_link/device_link_scheduler_job.h"

#include <atomic>
#include <mutex>
#include <string>

#include "esp_heap_caps.h"

#include "lunaredu_lunaros/kernel/log.h"
#include "lunaredu_lunaros/network/network.h"
#include "lunaredu_lunaros/network/network_jobs.h"
#include "lunaredu_lunaros/persistence/settings.h"
#include "device_link/device_link_http.h"
#include "device_link/device_link_operations.h"
#include "device_link/device_link_state.h"
#include "device_link/device_link_tls_policy.h"
#include "kernel/system_primitives.h"

namespace lunaredu::lunaros::device_link {
namespace {

constexpr const char *kTag = "device_link";
constexpr uint32_t kPresenceIntervalMs = 20000;
constexpr uint32_t kCommandPollIntervalMs = 5000;
constexpr uint32_t kDiscoveryPublishIntervalMs = 2500;
constexpr uint32_t kPairingPollIntervalMs = 3000;
// TLS certificate verification needs a contiguous internal-RAM scratch area.
// Retrying every few seconds while audio/MQTT is active only creates more
// pressure and can starve the realtime audio/render tasks.
constexpr uint32_t kRetryIntervalMs = 30000;

std::atomic<bool> g_pairing_requested{false};
std::atomic<bool> g_open_claim_enrolled{false};
std::atomic<uint32_t> g_last_open_claim_ms{0};
std::atomic<bool> g_pairing_cancel_requested{false};
std::atomic<bool> g_unbind_requested{false};
std::atomic<DeviceLinkUnbindState> g_unbind_state{DeviceLinkUnbindState::Idle};
std::atomic<uint32_t> g_discovery_requested_duration_ms{0};
std::mutex g_device_link_job_mutex;
DeviceLinkJobContext g_device_link_job;
std::atomic<uint32_t> g_last_presence_ms{0};
std::atomic<uint32_t> g_presence_interval_ms{kPresenceIntervalMs};
std::atomic<uint32_t> g_last_command_poll_ms{0};
std::atomic<uint32_t> g_last_discovery_publish_ms{0};
std::atomic<uint32_t> g_last_pairing_poll_ms{0};
std::atomic<bool> g_firmware_update_requested{false};
// 0 = none, 1 = production, 2 = development.
std::atomic<uint8_t> g_environment_switch_requested{0};
std::atomic<bool> g_environment_restart_pending{false};
std::atomic<bool> g_environment_restart_requested{false};

NetworkJobRunResult run_device_link_operation(NetworkJobHandle handle, void *) {
  DeviceLinkOperation operation = DeviceLinkOperation::None;
  uint32_t duration_ms = 0;
  {
    std::lock_guard<std::mutex> lock(g_device_link_job_mutex);
    if (g_device_link_job.handle.id != handle.id) {
      return {false, false, "device_link_job_stale"};
    }
    operation = g_device_link_job.operation;
    duration_ms = g_device_link_job.duration_ms;
  }

  bool succeeded = false;
  // Report completion independently of presence, which can replace the pairing
  // snapshot immediately after failure. Early admission failures also finish.
  struct UnbindCompletion {
    bool active;
    const bool &succeeded;
    ~UnbindCompletion() {
      if (active) g_unbind_state.store(succeeded ? DeviceLinkUnbindState::Succeeded
                                               : DeviceLinkUnbindState::Failed);
    }
  } completion{operation == DeviceLinkOperation::Unbind, succeeded};
  // Do this admission check before suspending XiaoZhi.  When the available
  // internal heap is already too fragmented for a TLS handshake, entering
  // and leaving cloud maintenance only tears down and recreates MQTT, which
  // can starve the assistant and turn one failed presence attempt into a
  // reconnect storm.
  const size_t internal_largest =
      heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
  const auto maintenance_gate = runtime_config().cloud_maintenance_gate;
  const DeviceLinkPairingSnapshot pairing = pairing_snapshot();
  // Interactive binding operations are allowed through this admission gate.
  // The cloud maintenance callback below suspends XiaoZhi and releases the
  // wake-word frontend before the TLS request.  Also allow the first presence
  // reconciliation while the local state is Connecting (for example after a
  // reboot following an unbind); once the server confirms Bound/Unbound,
  // periodic presence keeps the original low-memory backoff behavior.
  const bool allow_interactive_memory_recovery =
      operation == DeviceLinkOperation::OpenClaimEnrollment ||
      operation == DeviceLinkOperation::CancelPairing ||
      operation == DeviceLinkOperation::Unbind ||
      operation == DeviceLinkOperation::CreatePairing ||
      operation == DeviceLinkOperation::PollPairing ||
      operation == DeviceLinkOperation::FirmwareUpdate ||
      (operation == DeviceLinkOperation::Presence &&
       pairing.state == DeviceLinkBindingState::Connecting);
  // The local development gateway uses plain HTTP.  Requiring the TLS
  // scratch block for that transport leaves a bound device permanently
  // offline when the audio service has fragmented internal RAM, even though
  // the request itself does not need a TLS handshake.  Keep the guard for
  // production HTTPS only.
  const DeviceLinkTlsAdmission tls_admission = device_link_tls_admission(
      device_link::platform_is_production(), internal_largest,
      allow_interactive_memory_recovery);
  if (tls_admission == DeviceLinkTlsAdmission::Defer) {
    const uint32_t now = monotonic_ms();
    switch (operation) {
    case DeviceLinkOperation::Presence:
      g_last_presence_ms.store(now);
      g_presence_interval_ms.store(kRetryIntervalMs);
      break;
    case DeviceLinkOperation::Commands:
      g_last_command_poll_ms.store(now);
      break;
    case DeviceLinkOperation::PollPairing:
      g_last_pairing_poll_ms.store(now);
      break;
    case DeviceLinkOperation::Discovery:
      g_last_discovery_publish_ms.store(now);
      break;
    default:
      break;
    }
    std::lock_guard<std::mutex> lock(g_device_link_job_mutex);
    if (g_device_link_job.handle.id == handle.id) {
      g_device_link_job = {};
    }
    LOGW(kTag, "deferring cloud TLS: internal largest block=%u < %u",
         static_cast<unsigned>(internal_largest),
         static_cast<unsigned>(kNetworkMinimumTlsBlockBytes));
    return {false, true, "cloud_tls_internal_memory_low"};
  }

  bool maintenance_entered = false;
  // An admitted request already has the measured TLS scratch block, so it
  // must not tear down XiaoZhi merely to perform routine command, presence,
  // pairing, or discovery traffic. The maintenance gate is reserved for an
  // interactive request that must recover from a genuinely low-memory state.
  if (tls_admission == DeviceLinkTlsAdmission::Maintenance &&
      maintenance_gate != nullptr) {
    maintenance_entered = maintenance_gate(true);
    if (!maintenance_entered) {
      // Entry was rejected (for example because XiaoZhi is listening or
      // speaking). Do not emit an unmatched leave call: another foreground
      // owner or maintenance job may legitimately hold the suspension, and
      // ending it here races the first assistant reply back into reconnect.
      if (operation == DeviceLinkOperation::Presence) {
        g_last_presence_ms.store(monotonic_ms());
        g_presence_interval_ms.store(kRetryIntervalMs);
      }
      std::lock_guard<std::mutex> lock(g_device_link_job_mutex);
      if (g_device_link_job.handle.id == handle.id) {
        g_device_link_job = {};
      }
      return {false, true, "cloud_maintenance_not_ready"};
    }
  }
  switch (operation) {
  case DeviceLinkOperation::OpenClaimEnrollment:
    succeeded = enroll_open_claim();
    if (succeeded) {
      g_open_claim_enrolled.store(true);
    }
    break;
  case DeviceLinkOperation::CancelPairing:
    succeeded = !account_identity_ready() || cancel_pairing_session();
    g_last_pairing_poll_ms.store(0);
    break;
  case DeviceLinkOperation::Unbind:
    succeeded = unbind_device();
    g_last_presence_ms.store(0);
    g_last_command_poll_ms.store(0);
    break;
  case DeviceLinkOperation::CreatePairing:
    succeeded = create_pairing_session();
    g_last_pairing_poll_ms.store(monotonic_ms());
    break;
  case DeviceLinkOperation::Presence:
    succeeded = publish_presence();
    g_last_presence_ms.store(monotonic_ms());
    g_presence_interval_ms.store(succeeded ? kPresenceIntervalMs : kRetryIntervalMs);
    if (!succeeded) {
      LOGW(kTag, "presence publish failed; retrying in %u ms",
           static_cast<unsigned>(kRetryIntervalMs));
    }
    break;
  case DeviceLinkOperation::FirmwareUpdate:
    succeeded = refresh_and_apply_firmware_update();
    break;
  case DeviceLinkOperation::Commands:
    succeeded = poll_commands();
    g_last_command_poll_ms.store(monotonic_ms());
    break;
  case DeviceLinkOperation::PollPairing:
    succeeded = poll_pairing_session();
    g_last_pairing_poll_ms.store(monotonic_ms());
    break;
  case DeviceLinkOperation::StartDiscovery:
    succeeded = begin_discovery(duration_ms);
    // Preserve immediate publishing when a new discovery window is opened.
    g_last_discovery_publish_ms.store(0);
    break;
  case DeviceLinkOperation::Discovery:
    succeeded = publish_discovery_announcement();
    g_last_discovery_publish_ms.store(monotonic_ms());
    break;
  case DeviceLinkOperation::None:
    break;
  }

  if (maintenance_entered) {
    maintenance_gate(false);
  }

  {
    std::lock_guard<std::mutex> lock(g_device_link_job_mutex);
    if (g_device_link_job.handle.id == handle.id) {
      g_device_link_job = {};
    }
  }
  report_state(succeeded);
  return {succeeded, false,
          succeeded ? "device_link_operation_complete"
                    : "device_link_operation_failed"};
}

bool submit_device_link_operation(DeviceLinkOperation operation,
                                  uint8_t priority,
                                  uint32_t duration_ms = 0) {
  std::lock_guard<std::mutex> lock(g_device_link_job_mutex);
  if (g_device_link_job.handle) {
    return false;
  }
  std::string error;
  NetworkJobHandle handle;
  NetworkJobDefinition definition{
      "cloud.device_link", "device_link_operation", priority, 1, 0,
      run_device_link_operation, nullptr, false};
  if (operation == DeviceLinkOperation::FirmwareUpdate) {
    definition.job_class = NetworkJobClass::Ota;
  }
  if (!submit_network_job(
          definition, handle, error)) {
    LOGW(kTag, "failed to queue device link operation: %s", error.c_str());
    return false;
  }
  g_device_link_job.operation = operation;
  g_device_link_job.handle = handle;
  g_device_link_job.duration_ms = duration_ms;
  return true;
}

bool device_link_job_active() {
  std::lock_guard<std::mutex> lock(g_device_link_job_mutex);
  return static_cast<bool>(g_device_link_job.handle);
}

bool apply_environment_switch_if_requested() {
  const uint8_t requested = g_environment_switch_requested.exchange(0);
  if (requested == 0) {
    return false;
  }
  const bool production = requested == 1;
  const char *environment = production ? "production" : "development";
  std::string credential = device_config().cloud.credential;
  if (credential.empty()) {
    credential = create_pairing_nonce();
  }
  if (!save_device_cloud_config(environment, credential.c_str())) {
    update_pairing_snapshot(DeviceLinkBindingState::Error, {}, {}, 0,
                            "environment_save_failed");
    report_state(false);
    return true;
  }
  update_pairing_snapshot(DeviceLinkBindingState::Connecting);
  g_environment_restart_pending.store(true);
  LOGI(kTag,
       "platform environment changed environment=%s url=%s; awaiting restart",
       environment, platform_base_url());
  return true;
}

} // namespace

void tick_scheduler() {
  if (!device_link_job_active() && apply_environment_switch_if_requested()) {
    return;
  }
  if (g_environment_restart_pending.load()) {
    if (g_environment_restart_requested.exchange(false)) {
      LOGI(kTag, "restarting to apply platform environment=%s",
           platform_is_production() ? "production" : "development");
      internal::system_restart_after_delay(200);
    }
    // The persisted environment is now different from the active runtime.
    // Do not start another DeviceLink request until reboot rebuilds every
    // transport and private-resource authority from one coherent profile.
    return;
  }
  if (!network_connected()) {
    update_platform_connection(DeviceLinkPlatformConnectionState::Disconnected);
    report_network_device_link(
        account_identity_ready() ? NetworkDeviceLinkState::Connecting
                                 : NetworkDeviceLinkState::Unbound);
    return;
  }
  if (device_link_job_active()) {
    return;
  }
  if (g_pairing_cancel_requested.exchange(false)) {
    if (!submit_device_link_operation(DeviceLinkOperation::CancelPairing,
                                      kNetworkPriorityInteractive)) {
      g_pairing_cancel_requested.store(true);
    }
    return;
  }
  if (g_unbind_requested.exchange(false)) {
    if (!submit_device_link_operation(DeviceLinkOperation::Unbind,
                                      kNetworkPriorityInteractive)) {
      g_unbind_requested.store(true);
    }
    return;
  }
  if (g_pairing_requested.exchange(false)) {
    if (!submit_device_link_operation(DeviceLinkOperation::CreatePairing,
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
    if (!submit_device_link_operation(DeviceLinkOperation::StartDiscovery,
                                  kNetworkPriorityInteractive,
                                  discovery_duration_ms)) {
      g_discovery_requested_duration_ms.store(discovery_duration_ms);
    }
    return;
  }
  if (!account_identity_ready()) {
    if (pairing_state() != DeviceLinkBindingState::Error) {
      update_pairing_snapshot(DeviceLinkBindingState::Unbound);
    }
    report_state();
    return;
  }

  if (runtime_config().open_claim && !g_open_claim_enrolled.load()) {
    const uint32_t now = monotonic_ms();
    const uint32_t last = g_last_open_claim_ms.load();
    // Failed enrollment must not occupy the network I/O worker every tick.
    if (last == 0 || now - last >= kRetryIntervalMs) {
      g_last_open_claim_ms.store(now);
      (void)submit_device_link_operation(DeviceLinkOperation::OpenClaimEnrollment,
                                        kNetworkPriorityInteractive);
    }
    return;
  }

  if (g_firmware_update_requested.exchange(false)) {
    if (!submit_device_link_operation(DeviceLinkOperation::FirmwareUpdate,
                                      kNetworkPriorityCritical)) {
      g_firmware_update_requested.store(true);
    }
    return;
  }

  const DeviceLinkBindingState state = pairing_state();
  if (runtime_config().open_claim &&
      state == DeviceLinkBindingState::Unbound &&
      !discovery_active()) {
    (void)submit_device_link_operation(DeviceLinkOperation::StartDiscovery,
                                       kNetworkPriorityInteractive,
                                       kMaximumDiscoveryDurationMs);
    return;
  }

  const uint32_t now = monotonic_ms();
  const uint32_t last_presence = g_last_presence_ms.load();
  const uint32_t presence_interval = g_presence_interval_ms.load();
  if (last_presence == 0 || now - last_presence >= presence_interval) {
    if (!submit_device_link_operation(DeviceLinkOperation::Presence,
                                      kNetworkPriorityControl)) {
      g_last_presence_ms.store(now);
      g_presence_interval_ms.store(kRetryIntervalMs);
    }
    return;
  }
  const uint32_t last_commands = g_last_command_poll_ms.load();
  if (last_commands == 0 || now - last_commands >= kCommandPollIntervalMs) {
    (void)submit_device_link_operation(DeviceLinkOperation::Commands,
                                   kNetworkPriorityControl);
    return;
  }
  const uint32_t last_pairing = g_last_pairing_poll_ms.load();
  if (state == DeviceLinkBindingState::Pairing &&
      (last_pairing == 0 || now - last_pairing >= kPairingPollIntervalMs)) {
    (void)submit_device_link_operation(DeviceLinkOperation::PollPairing,
                                   kNetworkPriorityControl);
    return;
  }
  const uint32_t last_discovery = g_last_discovery_publish_ms.load();
  if (discovery_active() &&
      (last_discovery == 0 ||
       now - last_discovery >= kDiscoveryPublishIntervalMs)) {
    (void)submit_device_link_operation(DeviceLinkOperation::Discovery,
                                   kNetworkPriorityBackground);
  }
}

void reset_scheduler() {
  std::lock_guard<std::mutex> lock(g_device_link_job_mutex);
  if (g_device_link_job.handle) {
    std::string ignored;
    (void)cancel_network_job(g_device_link_job.handle, ignored);
  }
  g_device_link_job = {};
  g_pairing_requested.store(false);
  g_open_claim_enrolled.store(false);
  g_last_open_claim_ms.store(0);
  g_pairing_cancel_requested.store(false);
  g_unbind_requested.store(false);
  g_unbind_state.store(DeviceLinkUnbindState::Idle);
  g_discovery_requested_duration_ms.store(0);
  g_last_presence_ms.store(0);
  g_presence_interval_ms.store(kPresenceIntervalMs);
  g_last_command_poll_ms.store(0);
  g_last_discovery_publish_ms.store(0);
  g_last_pairing_poll_ms.store(0);
  g_firmware_update_requested.store(false);
  g_environment_switch_requested.store(0);
  g_environment_restart_pending.store(false);
  g_environment_restart_requested.store(false);
}

void request_pairing() {
  g_pairing_requested.store(true);
}

void request_pairing_cancel() {
  g_pairing_cancel_requested.store(true);
}

bool request_unbind() {
  auto state = g_unbind_state.load();
  do {
    if (state == DeviceLinkUnbindState::Pending) return false;
  } while (!g_unbind_state.compare_exchange_weak(state, DeviceLinkUnbindState::Pending));
  g_unbind_requested.store(true);
  return true;
}

DeviceLinkUnbindState unbind_state() {
  return g_unbind_state.load();
}

void request_discovery(uint32_t duration_ms) {
  g_discovery_requested_duration_ms.store(duration_ms);
}

void request_firmware_update() {
  g_firmware_update_requested.store(true);
}

bool request_environment_switch(bool production) {
  if (g_environment_restart_pending.load() ||
      g_environment_restart_requested.load()) {
    return false;
  }
  uint8_t expected = 0;
  return g_environment_switch_requested.compare_exchange_strong(
      expected, production ? 1 : 2);
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

} // namespace lunaredu::lunaros::device_link
