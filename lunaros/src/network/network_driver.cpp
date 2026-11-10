#include "network/network_service_internal.h"
#include "lunaredu_lunaros/network/network.h"

#include <array>
#include <atomic>
#include <string>

#include <esp_err.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <esp_timer.h>

#include "lunaredu_lunaros/kernel/log.h"
#include "lunaredu_lunaros/network/network_jobs.h"
#include "lunaredu_lunaros/persistence/settings.h"
#include "network/wifi/wifi_manager.h"
#include "network/espnow/espnow_internal.h"
#include "network/provisioning/provisioning_driver.h"
#include "network/provisioning/network_provisioning.h"
#include "network/time/network_time.h"

namespace lunaredu::lunaros {
namespace {

constexpr const char *kTag = "lunaros.esp32.net";
bool g_sync_clock_on_connect = true;
bool g_transition_initialized = false;
bool g_was_connected = false;
bool g_was_provisioning = false;
std::atomic<bool> g_clock_sync_started{false};

bool apply_initial_wifi_credentials(const NetworkPolicy &policy) {
  if (!policy.seed_initial_wifi_credentials_when_empty ||
      policy.initial_wifi_credential_count == 0) {
    return true;
  }
  const DeviceWifiConfig &wifi = device_config().wifi;
  if (wifi.configured || wifi.credential_count > 0) {
    return true;
  }
  if (policy.initial_wifi_credential_count >
      kNetworkInitialWifiCredentialCapacity) {
    LOGE(kTag, "initial Wi-Fi credential count exceeds LunarOS capacity");
    return false;
  }

  std::array<DeviceWifiCredential, kDeviceWifiCredentialMax> credentials{};
  for (uint8_t index = 0; index < policy.initial_wifi_credential_count;
       ++index) {
    credentials[index].ssid = policy.initial_wifi_credentials[index].ssid;
    credentials[index].password =
        policy.initial_wifi_credentials[index].password;
  }
  if (!save_device_wifi_credentials(credentials.data(),
                                    policy.initial_wifi_credential_count)) {
    LOGE(kTag, "failed to persist initial Wi-Fi credentials");
    return false;
  }
  LOGI(kTag, "seeded %u initial Wi-Fi credentials from LunarOS policy",
       static_cast<unsigned>(policy.initial_wifi_credential_count));
  return true;
}

void initialize_network_stack() {
  esp_err_t result = esp_netif_init();
  if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
    LOGE(kTag, "failed to initialize esp_netif: %s",
         esp_err_to_name(result));
  }

  result = esp_event_loop_create_default();
  if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
    LOGE(kTag, "failed to initialize default event loop: %s",
         esp_err_to_name(result));
  }
}

void observe_driver_transitions() {
  const WifiRuntimeSnapshot wifi = wifi_runtime_snapshot();
  const bool provisioning = network_provisioning_active();
  if (!g_transition_initialized || wifi.connected != g_was_connected) {
    if (wifi.connected) {
      LOGI(kTag, "network connected: %s", wifi.local_ip.c_str());
      if (g_sync_clock_on_connect) {
        const esp_err_t result = network_start_system_time_sync();
        g_clock_sync_started.store(result == ESP_OK);
        if (result != ESP_OK) {
          LOGW(kTag, "failed to start network time synchronization");
        }
      }
    } else if (g_transition_initialized) {
      LOGW(kTag, "network disconnected");
      if (!network_system_time_valid()) {
        g_clock_sync_started.store(false);
      }
    }
  }
  if ((!g_transition_initialized || provisioning != g_was_provisioning) &&
      provisioning) {
    LOGI(kTag, "network provisioning started");
  }
  g_transition_initialized = true;
  g_was_connected = wifi.connected;
  g_was_provisioning = provisioning;
}

bool start(void *, const lunaredu::lunaros::NetworkPolicy &policy,
           std::string &error) {
  if (policy.jobs.maximum_active_jobs >
      lunaredu::lunaros::kNetworkJobCapacity) {
    error = "network_job_policy_exceeds_capacity";
    return false;
  }
  g_sync_clock_on_connect = policy.synchronize_clock_on_connect;
  g_transition_initialized = false;
  g_was_connected = false;
  g_was_provisioning = false;
  g_clock_sync_started.store(false);
  if (!provisioning_driver_apply_softap_password_override(
          policy.provisioning.softap_password)) {
    error = "provisioning_softap_password_override_failed";
    return false;
  }
  if (!apply_initial_wifi_credentials(policy)) {
    error = "initial_wifi_credentials_apply_failed";
    return false;
  }
  initialize_network_stack();
  if (!setup_wifi(policy.station_enabled, policy.espnow.enabled, policy.espnow.channel)) {
    error = "network_wifi_start_failed";
    std::string stop_error;
    (void)stop_wifi(stop_error);
    return false;
  }
  if (!internal::start_espnow_on_owner(policy.espnow, error)) {
    std::string stop_error;
    (void)stop_wifi(stop_error);
    return false;
  }
  observe_driver_transitions();
  if (wifi_set_power_save_level(policy.initial_power_save)) {
    LOGI(kTag, "Wi-Fi power policy applied: %u", static_cast<unsigned>(policy.initial_power_save));
  } else {
    LOGW(kTag, "failed to apply Wi-Fi power policy");
  }
  return true;
}

bool stop(void *, std::string &error) {
  internal::stop_espnow_on_owner();
  stop_network_provisioning();
  network_stop_system_time_sync();
  g_clock_sync_started.store(false);
  g_transition_initialized = false;
  g_was_connected = false;
  g_was_provisioning = false;
  return stop_wifi(error);
}

void poll(void *) {
  tick_wifi();
  internal::poll_espnow_on_owner();
  observe_driver_transitions();
}

bool control_provisioning(
    void *, bool should_start,
    lunaredu::lunaros::NetworkProvisioningMode requested_mode,
    bool suspend_station,
    std::string &error) {
  error.clear();
  if (!should_start) {
    stop_network_provisioning();
    return true;
  }
  if (suspend_station) {
    wifi_set_station_suspended(true);
    wifi_disconnect_station();
  }
  if (!start_network_provisioning(requested_mode)) {
    if (suspend_station) {
      wifi_set_station_suspended(false);
    }
    error = "network_provisioning_start_failed";
    return false;
  }
  return true;
}

bool synchronize_clock(void *, uint32_t timeout_ms, bool force,
                       std::string &error) {
  g_clock_sync_started.store(true);
  const esp_err_t result = network_sync_system_time(timeout_ms, force);
  g_clock_sync_started.store(result == ESP_OK);
  if (result == ESP_OK) {
    error.clear();
    return true;
  }
  error = esp_err_to_name(result);
  return false;
}

bool apply_saved_credential(void *, uint8_t index, std::string &error) {
  if (!network_policy().station_enabled) {
    error = "network_station_disabled";
    return false;
  }
  const DeviceConfig &config = device_config();
  if (index >= config.wifi.credential_count) {
    error = "network_saved_credential_index_invalid";
    return false;
  }
  if (!wifi_queue_credentials_apply(config.wifi.credentials,
                                    config.wifi.credential_count, index)) {
    error = "network_saved_credential_apply_failed";
    return false;
  }
  error.clear();
  return true;
}

lunaredu::lunaros::NetworkObservation observe(void *) {
  const WifiRuntimeSnapshot wifi = wifi_runtime_snapshot();
  const bool provisioning_active = network_provisioning_active();
  lunaredu::lunaros::NetworkObservation observation;
  observation.ready = true;
  observation.credentials_available = wifi.credentials_available;
  observation.station_suspended = wifi.station_suspended;
  observation.link = wifi.connected
                         ? lunaredu::lunaros::NetworkLinkState::Up
                         : (!wifi.station_suspended && !wifi.target_ssid.empty() && !provisioning_active
                                ? lunaredu::lunaros::NetworkLinkState::Connecting
                                : lunaredu::lunaros::NetworkLinkState::Down);
  observation.rssi = wifi.connected ? wifi.rssi : 0;
  observation.provisioning =
      wifi.apply_pending
          ? lunaredu::lunaros::NetworkProvisioningState::Applying
          : (provisioning_active
                 ? lunaredu::lunaros::NetworkProvisioningState::Active
                 : lunaredu::lunaros::NetworkProvisioningState::Idle);
  observation.clock =
      network_system_time_valid()
          ? lunaredu::lunaros::NetworkClockState::Valid
          : (g_clock_sync_started.load()
                 ? lunaredu::lunaros::NetworkClockState::Synchronizing
                 : lunaredu::lunaros::NetworkClockState::Unsynchronized);
  observation.monotonic_ms =
      static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
  observation.connection_failure_started_at_ms =
      wifi.disconnect_started_at_ms != 0 ? wifi.disconnect_started_at_ms
                                         : wifi.connection_started_at_ms;
  observation.reconnect_attempts = wifi.reconnect_attempts;
  observation.local_ip = wifi.local_ip;
  observation.target_ssid = wifi.target_ssid;
  observation.provisioning_ssid = network_provisioning_softap_ssid();
  observation.provisioning_ip = wifi.softap_ip;
  observation.provisioning_password =
      provisioning_active
          ? provisioning_driver_softap_password()
          : std::string();
  return observation;
}

} // namespace

bool configure_network_service(const NetworkPolicy &policy,
                               const char *timezone, std::string &error) {
  if ((!policy.station_enabled && (policy.provisioning.enabled ||
          policy.synchronize_clock_on_connect || !policy.espnow.enabled)) ||
      (policy.espnow.enabled && (policy.espnow.channel == 0 || policy.espnow.channel > 14))) {
    error = "invalid_network_radio_policy";
    return false;
  }
  const esp_err_t timezone_result = network_set_system_timezone(timezone);
  if (timezone_result != ESP_OK) {
    error = std::string("network_timezone_configuration_failed:") +
            esp_err_to_name(timezone_result);
    return false;
  }
  const NetworkDriver driver = {
      start, stop, poll, observe, control_provisioning, synchronize_clock,
      apply_saved_credential, nullptr};
  return network_service_internal().bind(driver, policy, error);
}

} // namespace lunaredu::lunaros
