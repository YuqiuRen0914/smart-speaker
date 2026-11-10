#include "lunaros_network.h"
#include "lunaros_network_control.h"
#include "kernel/static_mutex.h"
#include "lunaros_network_local_access.h"
#include "internal/lunaros_network_local_access.h"
#include "network/bluetooth/bluetooth_lifecycle.h"
#include "network/local_access/captive_dns_server.h"
#include "network/wifi/wifi_analysis_internal.h"
#include "network/wifi/wifi_manager.h"
#include "configuration/wifi_credentials.h"
#include "sdkconfig.h"
#include <algorithm>
namespace lunaredu::lunaros {
namespace {
NetworkLocalAccessState state = NetworkLocalAccessState::Idle;
NetworkLocalAccessConfig config;
uint32_t generation = 0;
bool ap = false, ap_retirement_pending = false, ble = false, bt_pinned = false, bt_allowed = true,
     bt_stop_pending = false;
StaticMutex bt_view_mutex;
NetworkBluetoothControlSnapshot bt_view;
void publish_bluetooth_stop(const std::string &error) {
  StaticLockGuard lock(bt_view_mutex);
  bt_view.stop_pending = bt_stop_pending;
  const bool waiting = error == "bluetooth_stop_pending" || error == "bluetooth_sdk_retry_pending";
  if (!bt_stop_pending || !waiting || bt_view.error.empty()) bt_view.error = error;
}
// The logical feature owns its resource window. This owner only retires IO.
bool retire(std::string &error) {
  if (ap && !stop_captive_dns_server()) {
    error = "local_dns_stop_pending";
    return false;
  }
  if (ap || ap_retirement_pending) {
    if (!wifi_stop_softap()) {
      error = "local_ap_stop_pending";
      return false;
    }
    ap = false;
    ap_retirement_pending = false;
  }
  if (ble) {
    if (!network_local_gatt_stop(error))
      return false;
    if (!bt_pinned && !stop_bluetooth(error))
      return false;
    ble = false;
  }
  wifi_set_station_suspended(false);
  state = NetworkLocalAccessState::Idle;
  std::fill(config.password.begin(), config.password.end(), '\0');
  config = {};
  error.clear();
  return true;
}
} // namespace
bool network_local_access_active() {
  return state != NetworkLocalAccessState::Idle;
}
NetworkLocalAccessState network_local_access_state_on_owner() { return state; }
const char *network_local_access_ssid() {
  return ap ? config.ssid.c_str() : "";
}
bool open_network_local_access(KernelRuntime &runtime,
                               const NetworkLocalAccessConfig &value,
                               NetworkLocalAccessLease &lease,
                               std::string &error) {
  lease = {};
#if !CONFIG_LUNAROS_NETWORK_LOCAL_ACCESS
  (void)runtime;
  (void)value;
  (void)lease;
  error = "network_local_access_disabled";
  return false;
#endif
  if (state == NetworkLocalAccessState::Stopping || bt_stop_pending) {
    error = "network_local_access_stop_pending";
    return false;
  }
  if (state != NetworkLocalAccessState::Idle) {
    error = "network_local_access_busy";
    return false;
  }
  if (!valid_device_wifi_credentials(value.ssid, value.password) || value.password.empty()) {
    error = "invalid_local_access_config";
    return false;
  }
  if (value.ble && bt_allowed && !network_local_gatt_supported()) {
    error = "ble_provisioning_not_supported";
    return false;
  }
  if (!internal::wifi_monitor_shutdown(error))
    return false;
  ++generation;
  if (!generation)
    ++generation;
  lease.generation = generation;
  config = value;
  state = NetworkLocalAccessState::Preparing;
  if (value.suspend_station) {
    wifi_set_station_suspended(true);
    wifi_disconnect_station();
  }
  // The backend may change DHCP/mode or stop STA before reporting failure.
  // Reserve its retirement before the first SDK operation, not after success.
  ap_retirement_pending = true;
  if (!wifi_configure_softap_network(0xC0A80401u, 0xC0A80401u, 0xFFFFFF00u) ||
      !wifi_start_softap(value.ssid.c_str(), value.password.c_str())) {
    state = NetworkLocalAccessState::Stopping;
    (void)retire(error);
    error = "local_access_start_failed";
    return false;
  }
  ap = true;
  if (!start_captive_dns_server(runtime, 0xC0A80401u)) {
    state = NetworkLocalAccessState::Stopping;
    (void)retire(error);
    error = "local_dns_start_failed";
    return false;
  }
  if (value.ble && bt_allowed) {
    ble = true;
    if (!network_local_gatt_start(value.gatt, error)) {
      const auto failure = error;
      state = NetworkLocalAccessState::Stopping;
      (void)retire(error);
      error = failure;
      return false;
    }
  }
  state = captive_dns_server_state() == CaptiveDnsState::Ready
      ? NetworkLocalAccessState::Active : NetworkLocalAccessState::Preparing;
  error.clear();
  return true;
}
bool release_network_local_access(NetworkLocalAccessLease lease,
                                  std::string &error) {
  if (!lease.generation || lease.generation != generation) {
    error = "network_local_access_lease_stale";
    return false;
  }
  return close_network_local_access(error);
}
bool close_network_local_access(std::string &error) {
  if (state == NetworkLocalAccessState::Idle) {
    error.clear();
    return true;
  }
  state = NetworkLocalAccessState::Stopping;
  return retire(error);
}
bool stop_network_bluetooth_on_owner(std::string &error) {
  bt_stop_pending = true;
  bt_pinned = false;
  publish_bluetooth_stop("bluetooth_stop_pending");
  if (ble && !network_local_gatt_stop(error)) {
    publish_bluetooth_stop(error);
    return false;
  }
  if (!stop_bluetooth(error)) {
    publish_bluetooth_stop(error);
    return false;
  }
  ble = false;
  bt_stop_pending = false;
  error.clear();
  publish_bluetooth_stop(error);
  return true;
}
NetworkBluetoothControlSnapshot network_bluetooth_control_snapshot() {
  StaticLockGuard lock(bt_view_mutex);
  auto value = bt_view;
  value.enabled = bluetooth_running();
  return value;
}
bool network_bluetooth_stop_pending() {
  StaticLockGuard lock(bt_view_mutex);
  return bt_view.stop_pending;
}
bool set_network_bluetooth_on_owner(KernelRuntime &, bool enabled,
                                    std::string &error) {
  if (enabled) {
    if (bt_stop_pending) {
      error = "bluetooth_stop_pending";
      return false;
    }
    if (!internal::wifi_monitor_release_exclusive(error) ||
        !start_bluetooth(error))
      return false;
    bt_pinned = bt_allowed = true;
    error.clear();
    return true;
  }
  bt_allowed = false; // User intent; physical/runtime cleanup alone preserves it.
  (void)stop_network_bluetooth_on_owner(error);
  error.clear();
  return true;
}
void tick_network_local_access() {
  std::string error;
  if (bt_stop_pending)
    (void)stop_network_bluetooth_on_owner(error);
  if (state == NetworkLocalAccessState::Preparing || state == NetworkLocalAccessState::Active) {
    const auto dns = captive_dns_server_state();
    if (dns == CaptiveDnsState::Failed || dns == CaptiveDnsState::Stopped)
      state = NetworkLocalAccessState::Stopping;
    else if (dns == CaptiveDnsState::Ready)
      state = NetworkLocalAccessState::Active;
  }
  if (state == NetworkLocalAccessState::Stopping)
    (void)retire(error);
  else if (ble)
    (void)network_local_gatt_poll(error);
}
} // namespace lunaredu::lunaros
