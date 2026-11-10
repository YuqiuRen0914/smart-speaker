#include "lunaros_platform_link_service_id.h"
#include "lunaros_resource_service_id.h"
#include "lunaros_network_service_id.h"
#include "lunaros_ota_lifecycle.h"
#include "device_api/device_api_context.h"
#include "lunaros_platform_link_json.h"
#include "lunaros_platform_link.h"

#include <atomic>
#include <mutex>
#include "pairing_session.h"
#include "pairing_activity.h"
#include <sys/time.h>

#include "lunaros_network.h"
#include "kernel/service_executor.h"
#include "kernel/service.h"
#include "configuration/device_config.h"
#include "lunaredu_ota.h"
#include "platform_link_http.h"
#include "platform_link_operations.h"
#include "platform_link_ota_retry.h"
#include "platform_link_scheduler.h"
#include "platform_link_environment.h"
#include "platform_link_state.h"

namespace lunaredu::lunaros {
bool install_platform_link_setup_endpoint(KernelRuntime &runtime);
void remove_platform_link_setup_endpoint(KernelRuntime &runtime);
namespace {

std::atomic<bool> g_started{false};
std::atomic<bool> g_stopping{false};
std::mutex pairing_mutex;
uint64_t pairing_generation = 0;

bool start_platform_link_runtime(const PlatformLinkConfig &config) {
  bool expected = false;
  if (!g_started.compare_exchange_strong(expected, true)) {
    return true;
  }

  if (config.open_claim && device_cloud_config().credential.empty()) {
    const std::string credential = platform_link::create_pairing_nonce();
    const DeviceCloudConfig current = device_cloud_config();
    const char *environment = current.environment == "development"
                                  ? "development" : "production";
    if (!save_device_cloud_config(environment, credential.c_str())) {
      platform_link::update_pairing_snapshot(
          PlatformLinkBindingState::Error, {}, {}, 0,
          "open_claim_identity_save_failed");
      g_started.store(false);
      return false;
    }
    platform_link::request_open_claim_enrollment();
  }
  platform_link::configure_resource_transport();
  std::string restore_error;
  // A failed cache is unknown authority, not a reason to disable the only
  // authenticated path that can rebuild it. Local grants remain denied;
  // load_cached_binding publishes Error and suspends private scope on failure.
  const bool binding_restored=platform_link::load_cached_binding(restore_error);
  if(!device_cloud_config().credential.empty() && !platform_link::account_identity_ready()) {
    platform_link::update_pairing_snapshot(PlatformLinkBindingState::Error,{}, {},0,
                                         "device_credential_invalid");
  }
  if (config.observer && config.observer->restore) {
    config.observer->restore(platform_link::pairing_snapshot());
  }
  // Keep a confirmed cached identity in Bound state while offline, except a
  // durable revocation intent that still requires server reconciliation. Presence
  // will replace it with Unbound/Error when the server can be reached and
  // explicitly says the relationship no longer exists.
  if (binding_restored && platform_link::account_identity_ready() &&
      platform_link::pairing_snapshot().account_id.empty() &&
      !platform_link::binding_revocation_requested() &&
      !platform_link::pairing_snapshot().binding_claim_required) {
    platform_link::update_pairing_snapshot(PlatformLinkBindingState::Connecting);
  }

  return true;
}

ServiceStartResult start_platform_link_service(void *context) {
  if (platform_link::runtime_device() != context) {
    return {ServiceStartDisposition::Failed,
            "platform_link_not_configured"};
  }
  if (g_stopping.load()) {
    return {ServiceStartDisposition::Failed, "platform_link_stop_pending"};
  }
  if(!install_platform_link_setup_endpoint(static_cast<DeviceApiContext *>(context)->runtime))return {ServiceStartDisposition::Failed,"platform_link_endpoint_conflict"};
  if(!start_platform_link_runtime(platform_link::runtime_config())){
    remove_platform_link_setup_endpoint(static_cast<DeviceApiContext *>(context)->runtime);
    return {ServiceStartDisposition::Failed,"platform_link_start_failed"};
  }
  return {ServiceStartDisposition::Ready,"platform_link_ready"};
}

bool stop_platform_link_service(void *context, std::string &error) {
  if (platform_link::runtime_device() != context) {
    error = "platform_link_runtime_owner_mismatch";
    return false;
  }
  g_stopping.store(true);
  remove_platform_link_setup_endpoint(static_cast<DeviceApiContext *>(context)->runtime);
  if (!platform_link::reset_scheduler()) {
    error = "platform_link_job_stop_pending";
    return false;
  }
  platform_link::stop_realtime_control_channel();
  g_started.store(false);
  g_stopping.store(false);

  error.clear();
  return true;
}

} // namespace

bool configure_platform_link_runtime(DeviceApiContext &runtime, const PlatformLinkConfig &config,
                                   std::string &error) {
  error.clear();
  if (g_started.load()) {
    error = "platform_link_already_started";
    return false;
  }
  if (config.board_type.empty() || config.model.empty()) {
    error = "invalid_platform_link_config";
    return false;
  }
  platform_link::configure_runtime(runtime, config);
  return true;
}

bool reset_platform_link_local_binding(std::string &error) {
  g_stopping.store(true);
  if (auto *device = platform_link::runtime_device()) remove_platform_link_setup_endpoint(device->runtime);
  if (!platform_link::reset_scheduler()) {
    error = "platform_link_job_stop_pending";
    return false;
  }
  platform_link::stop_realtime_control_channel();
  g_started.store(false);
  const bool committed = platform_link::reset_local_binding(error);
  g_stopping.store(false);
  return committed;
}

ServiceNode platform_link_service_node(DeviceApiContext &runtime) {
  static constexpr ServiceDependency kDependencies[] = {
      {kKernelReadyServiceId, true},
      {kResourceServiceId, true},
      {kNetworkRuntimeServiceId, false},
  };
  return {
      {kPlatformLinkServiceId, "Platform Link", kMainServiceExecutor,
       ServiceCriticality::Optional, kDependencies,
       sizeof(kDependencies) / sizeof(kDependencies[0])},
      start_platform_link_service,
      stop_platform_link_service,
      &runtime,
  };
}

void tick_platform_link_runtime() {
  if (!g_started.load() || g_stopping.load()) return;
  if (ota_service_boot_pending()) {
    // Management health may itself require authoritative binding restoration.
    // Suppressing that work until confirmation creates a permanent cycle.
    if (platform_link::local_management_access_required() && !platform_link::local_authority_ready())
      platform_link::tick_boot_authority_recovery();
    return;
  }
  platform_link::tick_realtime_control_channel();
  platform_link::tick_scheduler();
}

namespace {
bool request_pairing_locked() {
  if (platform_link::environment_switch_pending() || platform_link::pairing_cancel_pending()) return false;
  if (!platform_link::network_connected()) {
    platform_link::update_pairing_snapshot(PlatformLinkBindingState::Error,
                                         {}, {}, 0,
                                         "wifi_not_connected");
    return false;
  }
  const PlatformLinkPairingSnapshot current =
      platform_link::pairing_snapshot();
  if (current.state == PlatformLinkBindingState::Bound ||
      current.state == PlatformLinkBindingState::Pairing || platform_link::pairing_request_pending()) {
    return false;
  }
  if (pairing_generation == UINT64_MAX) return false;
  ++pairing_generation;
  if (!current.revocation_requested)
    platform_link::update_pairing_snapshot(PlatformLinkBindingState::Connecting);
  platform_link::request_pairing();
  return true;
}


bool refresh_pairing_locked() {
  if (platform_link::environment_switch_pending() ||
      !platform_link::network_connected() || platform_link::pairing_request_pending() || platform_link::pairing_cancel_pending()) return false;
  const PlatformLinkPairingSnapshot current = platform_link::pairing_snapshot();
  if (current.state != PlatformLinkBindingState::Pairing ||
      current.code.empty()) return false;
  platform_link::request_pairing();
  return true;
}

bool cancel_pairing_locked() {
  const PlatformLinkPairingSnapshot current =
      platform_link::pairing_snapshot();
  if (current.state != PlatformLinkBindingState::Pairing && !platform_link::pairing_request_pending()) {
    return false;
  }
  platform_link::request_pairing_cancel();
  return true;
}

}
bool request_platform_link_pairing() { std::lock_guard<std::mutex> lock(pairing_mutex); return request_pairing_locked(); }
bool refresh_platform_link_pairing() { std::lock_guard<std::mutex> lock(pairing_mutex); return refresh_pairing_locked(); }
bool cancel_platform_link_pairing() { std::lock_guard<std::mutex> lock(pairing_mutex); return cancel_pairing_locked(); }
bool platform_link_pairing_pending() { return platform_link::pairing_request_pending() || platform_link::pairing_cancel_pending(); }
namespace platform_link {
PairingActivityObservation observe_pairing_session() {
  std::lock_guard<std::mutex> lock(pairing_mutex);
  timeval now{}; gettimeofday(&now,nullptr);
  if (!g_started.load() || g_stopping.load()) {
    PairingActivityObservation out{};out.session=pairing_generation;
    out.state.error="platform_pairing_not_ready";out.now_ms=int64_t(now.tv_sec)*1000+now.tv_usec/1000;return out;
  }
  return {platform_link_pairing_presentation(),pairing_generation,platform_link_pairing_pending(),int64_t(now.tv_sec)*1000+now.tv_usec/1000};
}
uint64_t pairing_session() { std::lock_guard<std::mutex> lock(pairing_mutex); return pairing_generation; }
bool claim_pairing_session(bool refresh, uint64_t &session, bool &owned, std::string &error) {
  std::lock_guard<std::mutex> lock(pairing_mutex);
  if (!g_started.load() || g_stopping.load()) { error="platform_pairing_not_ready";return false; }
  if (refresh && session != pairing_generation) { error = "stale_pairing_session"; return false; }
  if (pairing_cancel_pending()) { error = "pairing_cancel_pending"; return false; }
  const auto current = pairing_snapshot();
  const bool active = current.state == PlatformLinkBindingState::Pairing || pairing_request_pending();
  if (refresh ? !refresh_pairing_locked() : !active && !request_pairing_locked()) { error = "pairing_unavailable"; return false; }
  session = pairing_generation; owned = refresh || !active; error.clear(); return true;
}
bool release_pairing_session(uint64_t session, std::string &error) {
  std::lock_guard<std::mutex> lock(pairing_mutex);
  if (!session || session != pairing_generation) { error = "stale_pairing_session"; return false; }
  if (pairing_cancel_pending()) { error.clear(); return true; }
  if (!cancel_pairing_locked()) { error = "pairing_cancel_unavailable"; return false; }
  error.clear(); return true;
}
}
bool request_platform_link_unbind() {
  const PlatformLinkPairingSnapshot current =
      platform_link::pairing_snapshot();
  if (current.state == PlatformLinkBindingState::Unbound && current.persistence_pending)
    return platform_link::request_unbind();
  if (current.state != PlatformLinkBindingState::Bound && !current.revocation_requested) {
    return false;
  }
  return platform_link::request_unbind();
}

PlatformLinkUnbindState platform_link_unbind_state() {
  return platform_link::unbind_state();
}

PlatformLinkPairingSnapshot platform_link_pairing_snapshot() {
  return platform_link::pairing_snapshot();
}

PlatformLinkBindingStatus platform_link_binding_status() {
  return platform_link::binding_status();
}

bool platform_link_local_authority_ready() {return platform_link::local_authority_ready();}
bool platform_link_local_management_access_required(){return platform_link::local_management_access_required();}
PlatformLinkPairingPresentation platform_link_pairing_presentation(){return platform_link::pairing_presentation();}

std::string platform_link_account_identity() {
  return platform_link::account_identity();
}
PlatformLinkAccountScope platform_link_account_scope(){return platform_link::account_scope();}

float platform_link_pairing_remaining_fraction(uint32_t current_ms) {
  return platform_link::pairing_remaining_fraction(current_ms);
}

bool request_platform_link_firmware_update(std::string &error) {
  if (!platform_link::runtime_config().firmware_updates_enabled) {
    error = "cloud_firmware_disabled";
    return false;
  }
  if (!platform_link::network_connected()) {
    error = "ota_ticket_network_unavailable";
    ota_service_report_failure(error.c_str());
    return false;
  }
  if (!platform_link::account_identity_ready()) {
    error = "ota_ticket_identity_unavailable";
    ota_service_report_failure(error.c_str());
    return false;
  }
  const auto ota = ota_service_status_snapshot();
  if (ota.upload_active || ota.pull_active || ota.pull_queued || ota.restart_pending ||
      platform_link::firmware_check_state() == PlatformLinkFirmwareCheckState::Pending ||
      platform_link::firmware_update_pending()) {
    error = "ota_update_busy";
    return false;
  }
  // This entry is an explicit UI/authorized Device API action. Background
  // assignments call the scheduler directly and cannot clear quarantine.
  if (!platform_link::reset_automatic_ota_retry(error)) return false;
  ota_service_report_preparing("ota_ticket_refresh_queued");
  platform_link::request_firmware_update();
  error.clear();
  return true;
}

bool request_platform_link_firmware_check(std::string &error) {
  if (!platform_link::runtime_config().firmware_updates_enabled) error = "cloud_firmware_disabled";
  else if (!platform_link::network_connected()) error = "ota_ticket_network_unavailable";
  else if (!platform_link::account_identity_ready()) error = "ota_ticket_identity_unavailable";
  else {
    const auto ota = ota_service_status_snapshot();
    if (ota.upload_active || ota.pull_active || ota.pull_queued || ota.restart_pending ||
        !platform_link::request_firmware_check()) error = "ota_update_busy";
    else { error.clear(); return true; }
  }
  return false;
}

PlatformLinkFirmwareCheckState platform_link_firmware_check_state() {
  return platform_link::firmware_check_state();
}

PlatformLinkPlatformConnectionSnapshot
platform_link_platform_connection_snapshot() {
  return platform_link::platform_connection_snapshot();
}
PlatformLinkPlatformConnectionState platform_link_platform_connection_state(){return platform_link::platform_connection_state();}
bool platform_link_ui_snapshot_into(PlatformLinkUiSnapshot &out){return platform_link::ui_snapshot_into(out);}

PlatformLinkEnvironment platform_link_environment() {
  return platform_link::platform_is_production()
             ? PlatformLinkEnvironment::Production
             : PlatformLinkEnvironment::Development;
}

std::string platform_link_platform_base_url() {
  return platform_link::platform_base_url();
}

std::string platform_link_binding_browser_origin() {
  return platform_link::binding_browser_origin(platform_link::platform_base_url());
}

bool platform_link_uses_remote_base_url() {
  return platform_link::runtime_config().remote_base_url != nullptr;
}

bool request_platform_link_environment(PlatformLinkEnvironment environment,
                                     std::string &error,
                                     const std::string &development_base_url) {
  if (environment != PlatformLinkEnvironment::Production &&
      environment != PlatformLinkEnvironment::Development) {
    error = "invalid_platform_link_environment";
    return false;
  }
  if (ota_service_boot_pending()) {
    error = "environment_boot_verification_pending";
    return false;
  }
  const auto binding = platform_link::pairing_snapshot();
  if (binding.persistence_pending || binding.revocation_requested) {
    error = "platform_link_environment_switch_busy";
    return false;
  }
  if (binding.state == PlatformLinkBindingState::Pairing ||
      platform_link::environment_switch_pending()) {
    error = "platform_link_environment_switch_busy";
    return false;
  }
  std::string origin;
  if (environment == PlatformLinkEnvironment::Development) {
    auto requested = development_base_url.empty()
        ? device_cloud_config().development_base_url : development_base_url;
    if (requested.empty()) requested = platform_link::runtime_config().development_base_url;
    if (!platform_link::normalize_development_origin(requested, origin,platform_link::runtime_config().production_base_url)) {
      error = "invalid_development_base_url";
      return false;
    }
    const auto &production = platform_link::runtime_config().production_base_url;
    if (origin == (production.empty() ? platform_link::kDefaultProductionDeviceOrigin : production)) {
      error = "invalid_development_base_url";
      return false;
    }
  }
  if (environment == platform_link_environment() &&
      (environment == PlatformLinkEnvironment::Production || origin == platform_link::platform_base_url())) {
    error.clear();
    return true;
  }
  if (!platform_link::request_environment_switch(
          environment == PlatformLinkEnvironment::Production, origin)) {
    error = "platform_link_environment_switch_busy";
    return false;
  }
  error.clear();
  return true;
}

bool platform_link_environment_switch_pending() {
  return platform_link::environment_switch_pending();
}

bool platform_link_environment_restart_pending() {
  return platform_link::environment_restart_pending();
}

bool request_platform_link_environment_restart(std::string &error) {
  if (!platform_link::environment_restart_pending()) {
    error = "platform_link_environment_restart_not_pending";
    return false;
  }
  if (!platform_link::request_environment_restart()) {
    error = "platform_link_environment_restart_busy";
    return false;
  }
  error.clear();
  return true;
}

bool start_platform_link_lan_discovery(uint32_t duration_ms) {
  if (!platform_link::account_identity_ready() ||
      !platform_link::network_connected()) {
    return false;
  }
  const uint32_t bounded_duration =
      std::min(duration_ms, platform_link::kMaximumDiscoveryDurationMs);
  if (bounded_duration == 0) {
    return false;
  }
  platform_link::request_discovery(bounded_duration);
  return true;
}

} // namespace lunaredu::lunaros
