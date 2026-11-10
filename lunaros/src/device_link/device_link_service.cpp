#include "lunaredu_lunaros/device_link/device_link_json.h"
#include "lunaredu_lunaros/device_link/device_link.h"

#include <algorithm>
#include <atomic>
#include <cctype>

#include "lunaredu_lunaros/network/network.h"
#include "lunaredu_lunaros/kernel/service_executor.h"
#include "lunaredu_lunaros/kernel/service_host.h"
#include "lunaredu_lunaros/kernel/service_ids.h"
#include "lunaredu_lunaros/persistence/settings.h"
#include "lunaredu_lunaros/delivery/update.h"
#include "device_link/device_link_http.h"
#include "device_link/device_link_operations.h"
#include "device_link/device_link_scheduler.h"
#include "device_link/device_link_state.h"

namespace lunaredu::lunaros {
namespace {

std::atomic<bool> g_started{false};
DeviceLinkConfig g_config;
bool g_configured = false;

bool start_device_link_runtime(const DeviceLinkConfig &config) {
  bool expected = false;
  if (!g_started.compare_exchange_strong(expected, true)) {
    return true;
  }

  device_link::configure_runtime(config);
  if (config.open_claim && !device_link::account_identity_ready()) {
    const std::string credential = device_link::create_pairing_nonce();
    const DeviceConfig &current = device_config();
    const char *environment = current.cloud.environment == "development"
                                  ? "development" : "production";
    if (!save_device_cloud_config(environment, credential.c_str())) {
      device_link::update_pairing_snapshot(
          DeviceLinkBindingState::Error, {}, {}, 0,
          "open_claim_identity_save_failed");
      g_started.store(false);
      return false;
    }
  }
  device_link::configure_resource_transport();
  device_link::load_cached_binding();
  if (config.observer && config.observer->restore) {
    config.observer->restore(device_link::pairing_snapshot());
  }
  // Keep a confirmed cached identity in Bound state while offline. Presence
  // will replace it with Unbound/Error when the server can be reached and
  // explicitly says the relationship no longer exists.
  if (device_link::account_identity_ready() &&
      device_link::pairing_snapshot().account_id.empty()) {
    device_link::update_pairing_snapshot(DeviceLinkBindingState::Connecting);
  }
  device_link::report_state();
  return true;
}

ServiceStartResult start_device_link_service(void *) {
  if (!g_configured) {
    return {ServiceStartDisposition::Failed,
            "device_link_not_configured"};
  }
  return start_device_link_runtime(g_config)
             ? ServiceStartResult{ServiceStartDisposition::Ready,
                                  "device_link_ready"}
             : ServiceStartResult{ServiceStartDisposition::Failed,
                                  "device_link_start_failed"};
}

bool stop_device_link_service(void *, std::string &error) {
  g_started.store(false);
  device_link::reset_scheduler();
  report_network_device_link(NetworkDeviceLinkState::Disabled);
  error.clear();
  return true;
}

} // namespace

bool configure_device_link_runtime(const DeviceLinkConfig &config,
                                   std::string &error) {
  error.clear();
  if (g_started.load()) {
    error = "device_link_already_started";
    return false;
  }
  if (config.board_type.empty() || config.model.empty()) {
    error = "invalid_device_link_config";
    return false;
  }
  if (!config.fixed_pairing_code.empty() &&
      (config.fixed_pairing_code.size() != 6 ||
       !std::all_of(config.fixed_pairing_code.begin(),
                    config.fixed_pairing_code.end(),
                    [](unsigned char value) { return std::isdigit(value); }))) {
    error = "invalid_fixed_pairing_code";
    return false;
  }
  g_config = config;
  g_configured = true;
  return true;
}

ServiceNode device_link_service_node() {
  static constexpr ServiceDependency kDependencies[] = {
      {kSettingsServiceId, true},
      {kResourceServiceId, true},
      {kNetworkRuntimeServiceId, false},
  };
  return {
      {kDeviceLinkServiceId, "Device Link", kMainServiceExecutor,
       ServiceCriticality::Optional, kDependencies,
       sizeof(kDependencies) / sizeof(kDependencies[0])},
      start_device_link_service,
      stop_device_link_service,
  };
}

void tick_device_link_runtime() {
  if (g_started.load()) {
    device_link::tick_scheduler();
  }
}

bool request_device_link_pairing() {
  if (!device_link::network_connected()) {
    device_link::update_pairing_snapshot(DeviceLinkBindingState::Error,
                                         {}, {}, 0,
                                         "wifi_not_connected");
    return false;
  }
  const DeviceLinkPairingSnapshot current =
      device_link::pairing_snapshot();
  if (current.state == DeviceLinkBindingState::Bound ||
      current.state == DeviceLinkBindingState::Pairing) {
    return false;
  }
  if (!device_link::account_identity_ready()) {
    const std::string credential = device_link::create_pairing_nonce();
    const bool production = device_link::platform_is_production();
    if (!save_device_cloud_config(production ? "production" : "development",
                                  credential.c_str())) {
      device_link::update_pairing_snapshot(DeviceLinkBindingState::Error,
                                           {}, {}, 0,
                                           "identity_save_failed");
      return false;
    }
  }
  device_link::update_pairing_snapshot(
      DeviceLinkBindingState::Connecting);
  device_link::request_pairing();
  return true;
}

bool cancel_device_link_pairing() {
  const DeviceLinkPairingSnapshot current =
      device_link::pairing_snapshot();
  if (current.state != DeviceLinkBindingState::Pairing) {
    return false;
  }
  device_link::request_pairing_cancel();
  return true;
}

bool request_device_link_unbind() {
  const DeviceLinkPairingSnapshot current =
      device_link::pairing_snapshot();
  if (current.state != DeviceLinkBindingState::Bound ||
      !device_link::network_connected()) {
    return false;
  }
  return device_link::request_unbind();
}

DeviceLinkUnbindState device_link_unbind_state() {
  return device_link::unbind_state();
}

DeviceLinkPairingSnapshot device_link_pairing_snapshot() {
  return device_link::pairing_snapshot();
}

bool request_device_link_firmware_update(std::string &error) {
  if (!device_link::network_connected()) {
    error = "ota_ticket_network_unavailable";
    ota_service_report_failure(error.c_str());
    return false;
  }
  if (!device_link::account_identity_ready()) {
    error = "ota_ticket_identity_unavailable";
    ota_service_report_failure(error.c_str());
    return false;
  }
  if (ota_service_upload_active() || ota_service_restart_pending()) {
    error = "ota_update_busy";
    return false;
  }
  ota_service_report_preparing("ota_ticket_refresh_queued");
  device_link::request_firmware_update();
  error.clear();
  return true;
}

DeviceLinkPlatformConnectionSnapshot
device_link_platform_connection_snapshot() {
  return device_link::platform_connection_snapshot();
}

DeviceLinkEnvironment device_link_environment() {
  return device_link::platform_is_production()
             ? DeviceLinkEnvironment::Production
             : DeviceLinkEnvironment::Development;
}

const char *device_link_environment_name() {
  return device_link_environment() == DeviceLinkEnvironment::Production
             ? "production"
             : "development";
}

const char *device_link_platform_base_url() {
  return device_link::platform_base_url();
}

bool request_device_link_environment(DeviceLinkEnvironment environment,
                                     std::string &error) {
  if (environment != DeviceLinkEnvironment::Production &&
      environment != DeviceLinkEnvironment::Development) {
    error = "invalid_device_link_environment";
    return false;
  }
  if (environment == device_link_environment()) {
    error.clear();
    return true;
  }
  if (!device_link::request_environment_switch(
          environment == DeviceLinkEnvironment::Production)) {
    error = "device_link_environment_switch_busy";
    return false;
  }
  error.clear();
  return true;
}

bool device_link_environment_restart_pending() {
  return device_link::environment_restart_pending();
}

bool request_device_link_environment_restart(std::string &error) {
  if (!device_link::environment_restart_pending()) {
    error = "device_link_environment_restart_not_pending";
    return false;
  }
  if (!device_link::request_environment_restart()) {
    error = "device_link_environment_restart_busy";
    return false;
  }
  error.clear();
  return true;
}

bool start_device_link_lan_discovery(uint32_t duration_ms) {
  if (!device_link::account_identity_ready() ||
      !device_link::network_connected()) {
    return false;
  }
  const uint32_t bounded_duration =
      std::min(duration_ms, device_link::kMaximumDiscoveryDurationMs);
  if (bounded_duration == 0) {
    return false;
  }
  device_link::request_discovery(bounded_duration);
  return true;
}

} // namespace lunaredu::lunaros
