#include "lunaros_network_espnow.h"
#include "lunaros_network_policy.h"
#include "lunaros_network_local_access.h"
#include "network/network_service_internal.h"
#include "lunaros_network.h"
#include "lunaros_network_work_timing.h"
#include "kernel/runtime.h"

#include <array>
#include <atomic>
#include <string>

#include <esp_err.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <esp_timer.h>

#include <esp_log.h>
#include "sdkconfig.h"
#include "lunaros_network_jobs.h"
#include "network/http/tls_session_cache.h"
#include "network/http/http_connection_cache.h"
#include "configuration/device_config.h"
#include "network/wifi/wifi_manager.h"
#include "network/wifi/wifi_configuration_internal.h"
#include "network/wifi/wifi_analysis_internal.h"
#include "network/espnow/espnow_internal.h"
#include "internal/lunaros_network_local_access.h"
#include "network/time/network_time.h"
#include "system/boot_guard.h"
#include "network/bluetooth/bluetooth_lifecycle.h"
#include "lunaros_network_local_gatt.h"

namespace lunaredu::lunaros {
namespace {

constexpr const char *kTag = "lunaros.esp32.net";
bool g_sync_clock_on_connect = true;
bool g_transition_initialized = false;
bool g_was_connected = false;
bool g_was_local_access_active = false;
std::atomic<bool> g_clock_sync_started{false};

bool apply_initial_wifi_credentials(const NetworkPolicy &policy) {
  if ((!policy.seed_initial_wifi_credentials_when_empty &&
       !policy.force_initial_wifi_credentials) ||
      policy.initial_wifi_credential_count == 0) {
    return true;
  }
  const DeviceWifiConfig &wifi = device_wifi_config();
  if (!policy.force_initial_wifi_credentials &&
      (wifi.configured || wifi.credential_count > 0)) {
    return true;
  }
  if (policy.initial_wifi_credential_count >
      kNetworkInitialWifiCredentialCapacity) {
    ESP_LOGE(kTag, "initial Wi-Fi credential count exceeds LunarOS capacity");
    return false;
  }

  std::array<DeviceWifiCredential, kDeviceWifiCredentialMax> credentials{};
  for (uint8_t index = 0; index < policy.initial_wifi_credential_count;
       ++index) {
    credentials[index].ssid = policy.initial_wifi_credentials[index].ssid;
    credentials[index].password =
        policy.initial_wifi_credentials[index].password;
  }
  if (policy.force_initial_wifi_credentials &&
      wifi.credential_count == policy.initial_wifi_credential_count) {
    bool matches = true;
    for (uint8_t index = 0; index < wifi.credential_count; ++index) {
      matches = matches &&
          wifi.credentials[index].ssid == credentials[index].ssid &&
          wifi.credentials[index].password == credentials[index].password;
    }
    if (matches) return true;
  }
  if (!save_device_wifi_credentials(credentials.data(),
                                    policy.initial_wifi_credential_count)) {
    ESP_LOGE(kTag, "failed to persist initial Wi-Fi credentials");
    return false;
  }
  ESP_LOGI(kTag, "seeded %u initial Wi-Fi credentials from LunarOS policy",
       static_cast<unsigned>(policy.initial_wifi_credential_count));
  return true;
}

void initialize_network_stack() {
  esp_err_t result = esp_netif_init();
  if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(kTag, "failed to initialize esp_netif: %s",
         esp_err_to_name(result));
  }

  result = esp_event_loop_create_default();
  if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(kTag, "failed to initialize default event loop: %s",
         esp_err_to_name(result));
  }
}

void observe_driver_transitions() {
  const bool connected = wifi_connected();
  const bool local_access_active = network_local_access_active();
  if (!g_transition_initialized || connected != g_was_connected) {
    if (connected) {
      ESP_LOGI(kTag, "network connected: %s", wifi_runtime_snapshot().local_ip.c_str());
      if (g_sync_clock_on_connect) {
        const esp_err_t result = network_start_system_time_sync();
        g_clock_sync_started.store(result == ESP_OK);
        if (result != ESP_OK) {
          ESP_LOGW(kTag, "failed to start network time synchronization");
        }
      }
    } else if (g_transition_initialized) {
      invalidate_http_tls_session();
      ESP_LOGW(kTag, "network disconnected");
      if (!network_system_time_valid()) {
        g_clock_sync_started.store(false);
      }
    }
  }
  if ((!g_transition_initialized || local_access_active != g_was_local_access_active) &&
      local_access_active) {
    ESP_LOGI(kTag, "network local access started");
  }
  g_transition_initialized = true;
  g_was_connected = connected;
  g_was_local_access_active = local_access_active;
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
  g_was_local_access_active = false;
  g_clock_sync_started.store(false);
  if (!apply_initial_wifi_credentials(policy)) {
    error = "initial_wifi_credentials_apply_failed";
    return false;
  }
  reset_wifi_configuration_on_owner();
  initialize_network_stack();
  if (!setup_wifi(policy.station_enabled, policy.espnow.enabled, policy.espnow.channel,
                  policy.default_wifi_fallback_enabled &&
                      !policy.force_initial_wifi_credentials)) {
    error = "network_wifi_start_failed";
    std::string stop_error;
    (void)stop_wifi(stop_error);
    return false;
  }
  if (!internal::start_espnow_on_owner(policy.espnow, error)) {
    std::string stop_error;
    // Wi-Fi backs ESP-NOW. Retain it until ESP-NOW teardown is acknowledged.
    if (internal::stop_espnow_on_owner(stop_error)) (void)stop_wifi(stop_error);
    return false;
  }
  observe_driver_transitions();
  if (wifi_set_power_save_level(policy.initial_power_save)) {
    ESP_LOGI(kTag, "Wi-Fi power policy applied: %u", static_cast<unsigned>(policy.initial_power_save));
  } else {
    ESP_LOGW(kTag, "failed to apply Wi-Fi power policy");
  }
  return true;
}

bool stop(void *, std::string &error) {
  invalidate_http_tls_session();
  poll_http_connections();
  if(http_connection_owned()){
    error="network_http_retirement_pending";return false;
  }
  if (!internal::wifi_monitor_shutdown(error)) return false;
  if (!internal::stop_espnow_on_owner(error)) return false;
  if (!close_network_local_access(error)) {
    error = "network_local_access_stop_pending";
    return false;
  }
  if (!stop_network_bluetooth_on_owner(error)) {
    // Products without BLE have no radio to stop.
    if (error != "bluetooth_disabled") return false;
    error.clear();
  }
  network_stop_system_time_sync();
  g_clock_sync_started.store(false);
  g_transition_initialized = false;
  g_was_connected = false;
  g_was_local_access_active = false;
  if(!stop_wifi(error))return false;
  reset_wifi_configuration_on_owner();return true;
}

void poll(void *) {
#if CONFIG_LUNAROS_BOOT_GUARD
  system_boot_guard_heartbeat(BootGuardOwner::Network);
#endif
  network_work(NetworkWorkStage::Tls,[]{poll_http_connections();poll_http_tls_session();});
  network_work(NetworkWorkStage::WifiMonitor,[]{internal::wifi_monitor_poll(static_cast<uint32_t>(esp_timer_get_time() / 1000ULL));});
  network_work(NetworkWorkStage::Wifi,[]{tick_wifi();});
  network_work(NetworkWorkStage::Configuration,[]{poll_wifi_configuration_on_owner();});
  network_work(NetworkWorkStage::Bluetooth,[]{poll_bluetooth_profiles();});
  network_work(NetworkWorkStage::EspNow,[]{internal::poll_espnow_on_owner();});
  network_work(NetworkWorkStage::Transitions,[]{
    observe_driver_transitions();
    if (g_sync_clock_on_connect && wifi_connected()) network_poll_system_time_sync();
  });
}

bool control_local_access(void *context,bool start,const NetworkLocalAccessConfig &config,NetworkLocalAccessLease &lease,std::string &error) {
  return start?open_network_local_access(*static_cast<KernelRuntime *>(context),config,lease,error):release_network_local_access(lease,error);
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
  return apply_network_saved_wifi_on_owner(index, error);
}

void observe(void *, NetworkObservation &observation) {
  wifi_observe_network(observation, network_local_access_active());
  observation.platform_ready = true;
  observation.local_access = network_local_access_state_on_owner();
  observation.clock =
      network_system_time_valid()
          ? lunaredu::lunaros::NetworkClockState::Valid
          : (g_clock_sync_started.load()
                 ? lunaredu::lunaros::NetworkClockState::Synchronizing
                 : lunaredu::lunaros::NetworkClockState::Unsynchronized);
#if CONFIG_BT_NIMBLE_ENABLED || CONFIG_BT_BLUEDROID_ENABLED
  observation.bluetooth_available=true;
#else
  observation.bluetooth_available=false;
#endif
  observation.bluetooth_enabled=bluetooth_running();
  observation.bluetooth_connected=observation.bluetooth_enabled &&
      (network_local_gatt_connected() || bluetooth_connected());
  if (observation.local_access_ssid != network_local_access_ssid()) observation.local_access_ssid = network_local_access_ssid();
}

} // namespace

bool configure_network_service(KernelRuntime &runtime, const NetworkPolicy &policy,
                               const char *timezone, std::string &error) {
  if ((!policy.station_enabled && (policy.synchronize_clock_on_connect || !policy.espnow.enabled)) ||
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
      start, stop, poll, observe, control_local_access, synchronize_clock,
      apply_saved_credential, &runtime, &runtime};
  return network_service_internal().bind(driver, policy, error);
}

} // namespace lunaredu::lunaros
