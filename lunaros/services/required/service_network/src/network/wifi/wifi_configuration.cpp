#include "lunaros_network.h"
#include "configuration/device_config.h"
#include "kernel/runtime.h"
#include "kernel/service_executor.h"
#include "kernel/static_mutex.h"
#include "lunaros_network_wifi_configuration.h"
#include <mbedtls/platform_util.h>
#include "network/network_service_internal.h"
#include "network/wifi/wifi_analysis_internal.h"
#include "network/wifi/wifi_configuration_internal.h"
#include "network/wifi/wifi_credential_list.h"
#include "configuration/wifi_credentials.h"
#include "network/wifi/wifi_manager.h"
#include "network/wifi/wifi_manager_internal.h"
#include <algorithm>
#include <cstring>
#include <esp_timer.h>
namespace lunaredu::lunaros {
namespace {
StaticMutex mutex;
// Mutations belong to the serialized Network executor. Foreign readers only
// access this published view; storage and radio callbacks never hold its lock.
NetworkWifiConnectionSnapshot value;
NetworkWifiConnectionSnapshot published;
bool published_busy = false, mutation_active = false;
DeviceWifiCredential candidate;
WifiRuntimeSnapshot observation; // Owner-only; retains string capacity between polls.
uint32_t expected_generation = 0, next_attempt = 0;
void wipe(std::string &s) {
  if(!s.empty())mbedtls_platform_zeroize(s.data(),s.size());
  s.clear();
}
struct Credentials {
  DeviceWifiCredential items[kDeviceWifiCredentialMax];
  ~Credentials() {
    for (auto &i : items)
      wipe(i.password);
  }
};
bool busy() {
  return value.phase == NetworkWifiConnectionPhase::Connecting ||
         value.phase == NetworkWifiConnectionPhase::Saving;
}
void publish() {
  StaticLockGuard lock(mutex);
  if (published.sequence != value.sequence || published.attempt != value.attempt)
    published = value;
  published_busy = mutation_active || busy();
}
struct Mutation {
  bool admitted = !mutation_active;
  Mutation() { if (admitted) { mutation_active = true; publish(); } }
  ~Mutation() { if (admitted) { mutation_active = false; publish(); } }
};
void restore() {
  auto &driver = wifi_driver_state();
  // Queue retirement through the existing disconnect-before-apply path. An
  // empty saved list is valid; it restores the configured runtime fallback.
  const auto config = device_wifi_config();
  std::lock_guard<std::recursive_mutex> lock(driver.mutex);
  copy_wifi_credentials(config.credentials, config.credential_count,
                        driver.pending_credentials,
                        driver.pending_credential_count);
  driver.pending_preferred_index = config.last_success_index;
  driver.pending_temporary_credentials = false;
  driver.pending_apply = true;
}
void terminal(NetworkWifiConnectionPhase phase, const char *error,
              bool rollback) {
  value.phase = phase;
  value.error = error ? error : "";
  ++value.sequence;
  wipe(candidate.password);
  candidate.ssid.clear();
  if (rollback)
    restore();
}
bool admit(bool needs_station, std::string &error) {
  if (!admit_network_work(error)) return false;
  wifi_runtime_snapshot_into(observation);
  if (busy() || observation.apply_pending) {
    error = "wifi_configuration_busy"; return false;
  }
  const auto network = network_fast_snapshot();
  if (network.fixed_wifi_credentials) {
    error = "fixed_wifi_credentials"; return false;
  }
  if (needs_station && (!network.service_started || !network.station_enabled)) {
    error = "network_station_unavailable"; return false;
  }
  if (needs_station) {
    auto &driver = wifi_driver_state();
    std::lock_guard<std::recursive_mutex> lock(driver.mutex);
    if (!driver.stack_ready) { error = "network_station_unavailable"; return false; }
  }
  error.clear(); return true;
}
bool valid_list(const DeviceWifiCredential *items, size_t count, uint8_t index,
                std::string &error) {
  const auto validity=validate_device_wifi_list(items,count);
  if (validity!=DeviceWifiListValidity::Valid || index>=count) {
    error=validity==DeviceWifiListValidity::Duplicate?"duplicate_wifi_ssid":"invalid_wifi_credentials";
    return false;
  }
  error.clear(); return true;
}
bool apply_validated(const DeviceWifiCredential *items, size_t count, uint8_t index,
                     bool temporary, std::string &error) {
  if (!internal::wifi_monitor_shutdown(error)) return false;
  if (!wifi_queue_credentials_apply(items, count, index, temporary)) {
    error = "wifi_apply_failed"; return false;
  }
  error.clear(); return true;
}
enum class Kind { Connect, ConnectSaved, Cancel, Forget, Clear };
struct Request {
  Kind kind;
  const char *ssid = nullptr, *password = nullptr;
  uint32_t attempt = 0;
  NetworkWifiCredentialResult result = NetworkWifiCredentialResult::NotReady;
};
void execute(void *opaque, ServiceOperationResult &result) {
  auto &r = *static_cast<Request *>(opaque);
  Mutation mutation;
  if (!mutation.admitted) {
    r.result = NetworkWifiCredentialResult::Busy;
    result.ok = false; result.error = "wifi_configuration_busy"; return;
  }
  const auto network = network_fast_snapshot();
  if (!network.service_started || !network.station_enabled) {
    result.ok = false;
    result.error = "network_station_unavailable";
    return;
  }
  if (r.kind == Kind::Cancel) {
    if (!r.attempt || r.attempt != value.attempt) {
      result.ok = false;
      result.error = "wifi_attempt_not_found";
      return;
    }
    if (value.phase == NetworkWifiConnectionPhase::Connecting)
      terminal(NetworkWifiConnectionPhase::Cancelled, "", true);
    result.ok = true;
    return;
  }
  if (!admit(true, result.error)) {
    if (result.error == "wifi_configuration_busy") r.result = NetworkWifiCredentialResult::Busy;
    result.ok = false; return;
  }
  if (r.kind == Kind::Connect || r.kind == Kind::ConnectSaved) {
    const auto ssid = r.ssid ? std::string(r.ssid) : "";
    std::string password = r.password ? std::string(r.password) : "";
    if (r.kind == Kind::ConnectSaved) {
      const auto saved = device_wifi_config();
      bool found = false;
      for (uint8_t i = 0;
           i < saved.credential_count && i < kDeviceWifiCredentialMax; ++i)
        if (saved.credentials[i].ssid == ssid) {
          password = saved.credentials[i].password;
          found = true;
          break;
        }
      if (!found) {
        result.ok = false;
        result.error = "wifi_saved_network_not_found";
        return;
      }
    }
    if (ssid.empty()) {
      r.result = NetworkWifiCredentialResult::MissingSsid;
      result.ok = false;
      result.error = "missing_ssid";
      return;
    }
    if (!valid_device_wifi_credentials(ssid, password)) {
      r.result = NetworkWifiCredentialResult::InvalidCredentials;
      result.ok = false;
      result.error = "wifi_credentials_invalid";
      return;
    }
    candidate = {ssid, password};
    wipe(password);
    wifi_runtime_snapshot_into(observation);
    expected_generation = observation.configuration_generation + 1;
    if (!expected_generation)
      ++expected_generation;
    if (!apply_validated(&candidate, 1, 0, true, result.error)) {
      wipe(candidate.password);
      r.result = NetworkWifiCredentialResult::ApplyFailed;
      result.ok = false;
      return;
    }
    ++next_attempt;
    if (!next_attempt)
      ++next_attempt;
    value.attempt = next_attempt;
    value.phase = NetworkWifiConnectionPhase::Connecting;
    value.ssid = ssid;
    value.ip.clear();
    value.error.clear();
    value.started_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000);
    ++value.sequence;
    r.attempt = value.attempt;
    r.result = NetworkWifiCredentialResult::Ok;
    result.ok = true;
    return;
  }
  const auto config = device_wifi_config();
  if (r.kind == Kind::Forget &&
      (!r.ssid || !valid_device_wifi_credentials(r.ssid, ""))) {
    result.ok = false; result.error = "missing_ssid"; return;
  }
  Credentials kept;
  size_t count = 0;
  for (uint8_t i = 0;
       i < config.credential_count && i < kDeviceWifiCredentialMax; ++i)
    if (r.kind != Kind::Clear &&
        config.credentials[i].ssid != (r.ssid ? r.ssid : ""))
      kept.items[count++] = config.credentials[i];
  if (r.kind == Kind::Forget && count == config.credential_count) {
    result.ok = false; result.error = "wifi_saved_network_not_found"; return;
  }
  if (!internal::wifi_monitor_shutdown(result.error)) {
    result.ok = false; return;
  }
  if (!(count ? save_device_wifi_credentials(kept.items, count)
              : clear_device_wifi_credentials())) {
    result.ok = false;
    result.error = "wifi_save_failed";
    return;
  }
  const auto current = wifi_runtime_snapshot();
  if (r.kind == Kind::Clear || current.target_ssid == (r.ssid ? r.ssid : ""))
    restore();
  else {
    // Removing another remembered network must not interrupt this association.
    auto &driver = wifi_driver_state();
    std::lock_guard<std::recursive_mutex> driver_lock(driver.mutex);
    copy_wifi_credentials(kept.items, count, driver.credentials,
                          driver.credential_count);
    driver.temporary_credentials = false;
    append_default_wifi_fallback();
    for (uint8_t i = 0; i < driver.credential_count; ++i)
      if (driver.credentials[i].ssid == current.target_ssid) {
        driver.current_credential_index = i;
        driver.preferred_credential_index = i;
        break;
      }
  }
  result.ok = true;
}
bool invoke(KernelRuntime &runtime, Request &request, std::string &error) {
  if (!require_network_runtime(runtime, error))
    return false;
  return runtime.invoke_executor(kNetworkServiceExecutor, execute, &request,
                                 error);
}
} // namespace
bool apply_network_saved_wifi_on_owner(uint8_t index, std::string &error) {
  Mutation mutation;
  if (!mutation.admitted) { error = "wifi_configuration_busy"; return false; }
  if (!admit(true, error)) return false;
  const auto config = device_wifi_config();
  if (index >= config.credential_count) {
    error = "network_saved_credential_index_invalid"; return false;
  }
  if (!valid_list(config.credentials, config.credential_count, index, error)) return false;
  return apply_validated(config.credentials, config.credential_count, index, false, error);
}
bool set_network_wifi_config_on_owner(const DeviceWifiCredential *items, size_t count,
    const char *device_name, const char *ap_name, bool apply, uint8_t index,
    std::string &error) {
  Mutation mutation;
  if (!mutation.admitted) { error = "wifi_configuration_busy"; return false; }
  if (!admit(apply, error) || !valid_list(items, count, index, error)) return false;
  if (!save_device_network_config(items, count, device_name, ap_name)) {
    error = "save_failed"; return false;
  }
  return !apply || apply_validated(items, count, index, false, error);
}
NetworkSavedWifiSnapshot network_saved_wifi_snapshot() {
  const auto config = device_wifi_config();
  NetworkSavedWifiSnapshot result;
  result.count =
      std::min<size_t>(config.credential_count, result.credentials.size());
  result.configured = config.configured;
  result.fixed = network_fast_snapshot().fixed_wifi_credentials;
  result.last_success_index = config.last_success_index;
  for (size_t i = 0; i < result.count; ++i)
    result.credentials[i] = {config.credentials[i].ssid,
                             !config.credentials[i].password.empty()};
  return result;
}
NetworkWifiConnectionSnapshot network_wifi_connection_snapshot() {
  StaticLockGuard lock(mutex);
  return published;
}
bool wifi_configuration_busy() {
  StaticLockGuard lock(mutex);
  return published_busy;
}
NetworkWifiCredentialResult
request_network_connect_saved_wifi(KernelRuntime &runtime, const char *ssid,
                                   uint32_t &attempt) {
  Request request{Kind::ConnectSaved, ssid};
  std::string error;
  if (!invoke(runtime, request, error)) {
    attempt = 0;
    return request.result;
  }
  attempt = request.attempt;
  return request.result;
}
NetworkWifiCredentialResult request_network_connect_wifi(KernelRuntime &runtime,
                                                         const char *ssid,
                                                         const char *password,
                                                         uint32_t &attempt) {
  Request request{Kind::Connect, ssid, password};
  std::string error;
  if (!invoke(runtime, request, error)) {
    attempt = 0;
    return request.result;
  }
  attempt = request.attempt;
  return request.result;
}
bool request_network_cancel_wifi_connection(KernelRuntime &runtime,
                                            uint32_t attempt,
                                            std::string &error) {
  Request r{Kind::Cancel};
  r.attempt = attempt;
  return invoke(runtime, r, error);
}
bool request_network_forget_wifi(KernelRuntime &runtime, const char *ssid,
                                 std::string &error) {
  Request r{Kind::Forget, ssid};
  return invoke(runtime, r, error);
}
bool request_network_clear_saved_wifi(KernelRuntime &runtime,
                                      std::string &error) {
  Request r{Kind::Clear};
  return invoke(runtime, r, error);
}
void reset_wifi_configuration_on_owner() {
  Mutation mutation;
  if (!mutation.admitted) return;
  wipe(candidate.password);
  candidate = {};
  value = {};
  expected_generation = 0;
}
void poll_wifi_configuration_on_owner() {
  if (mutation_active || value.phase != NetworkWifiConnectionPhase::Connecting) return;
  Mutation mutation;
  if (!mutation.admitted) return;
  if (value.phase != NetworkWifiConnectionPhase::Connecting)
    return;
  wifi_runtime_snapshot_into(observation);
  const auto &wifi = observation;
  if (!wifi.apply_pending &&
      wifi.configuration_generation == expected_generation && wifi.connected &&
      wifi.target_ssid == value.ssid && !wifi.local_ip.empty()) {
    value.phase = NetworkWifiConnectionPhase::Saving;
    ++value.sequence;
    value.ip = wifi.local_ip;
    publish(); // Readers can observe Saving while NVS owns the slow operation.
    const auto saved = device_wifi_config();
    Credentials merged;
    const auto count = build_wifi_credential_list(
        candidate, saved.credentials, saved.credential_count,
        merged.items, kDeviceWifiCredentialMax);
    if (!save_device_wifi_credentials(merged.items, count)) {
      terminal(NetworkWifiConnectionPhase::ConnectedUnsaved, "wifi_save_failed",
               false);
      return;
    }
    auto &driver = wifi_driver_state();
    {
      std::lock_guard<std::recursive_mutex> driver_lock(driver.mutex);
      copy_wifi_credentials(merged.items, count, driver.credentials,
                            driver.credential_count);
      driver.temporary_credentials = false;
      append_default_wifi_fallback();
      driver.current_credential_index = driver.preferred_credential_index = 0;
    }
    terminal(NetworkWifiConnectionPhase::Connected, "", false);
    return;
  }
  const uint32_t now = static_cast<uint32_t>(esp_timer_get_time() / 1000);
  if (static_cast<uint32_t>(now - value.started_ms) >= 30000)
    terminal(NetworkWifiConnectionPhase::Failed,
             wifi.last_disconnect_reason == 202 ||
                     wifi.last_disconnect_reason == 204 ||
                     wifi.last_disconnect_reason == 15
                 ? "wifi_auth_failed"
                 : "wifi_connection_timeout",
             true);
}
} // namespace lunaredu::lunaros
