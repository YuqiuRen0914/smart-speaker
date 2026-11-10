#include "network/provisioning/provisioning_driver.h"

#include <esp_random.h>

#include "lunaredu_lunaros/persistence/settings.h"
#include "network/provisioning/captive_dns_server.h"
#include "network/provisioning/provisioning_softap_password.h"
#include "network/wifi/wifi_manager.h"

namespace lunaredu::lunaros {

namespace {

std::string load_or_create_softap_password() {
  std::string secret =
      lunaredu::lunaros::settings_get_string("lunar_net", "ap_secret");
  if (is_provisioning_softap_password(secret)) {
    return secret;
  }

  secret = make_provisioning_softap_password(esp_random());
  lunaredu::lunaros::settings_set_string("lunar_net", "ap_secret", secret);
  return secret;
}

} // namespace

ProvisioningWifiSnapshot provisioning_driver_wifi_snapshot() {
  const WifiRuntimeSnapshot wifi = wifi_runtime_snapshot();
  return {
      .connected = wifi.connected,
      .station_suspended = wifi.station_suspended,
      .softap_active = wifi.softap_active,
      .status_code = wifi.status_code,
      .last_disconnect_reason = wifi.last_disconnect_reason,
      .reconnect_attempts = wifi.reconnect_attempts,
      .disconnect_started_at_ms = wifi.disconnect_started_at_ms,
      .next_reconnect_delay_ms = wifi.next_reconnect_delay_ms,
      .rssi = wifi.rssi,
      .local_ip = wifi.local_ip,
      .target_ssid = wifi.target_ssid,
      .softap_ip = wifi.softap_ip,
  };
}

int32_t provisioning_driver_wifi_rssi() { return wifi_rssi(); }

int provisioning_driver_wifi_channel() { return wifi_channel(); }

bool provisioning_driver_station_suspended() {
  return wifi_station_suspended();
}

void provisioning_driver_set_station_suspended(bool suspended) {
  wifi_set_station_suspended(suspended);
}

void provisioning_driver_disconnect_station() { wifi_disconnect_station(); }

bool provisioning_driver_configure_softap(uint32_t ip, uint32_t gateway,
                                            uint32_t subnet) {
  return wifi_configure_softap_network(ip, gateway, subnet);
}

bool provisioning_driver_apply_softap_password_override(
    const std::string &password) {
  if (password.empty()) return true;
  if (!is_provisioning_softap_password(password)) return false;
  if (settings_get_string("lunar_net", "ap_secret") == password) return true;
  return settings_set_string("lunar_net", "ap_secret", password);
}

std::string provisioning_driver_softap_password() {
  return load_or_create_softap_password();
}

bool provisioning_driver_start_softap(const char *ssid) {
  const std::string password = load_or_create_softap_password();
  return wifi_start_softap(ssid, password.c_str());
}

void provisioning_driver_stop_softap() { wifi_stop_softap(); }

void provisioning_driver_start_captive_dns(uint32_t ip) {
  start_captive_dns_server(ip);
}

void provisioning_driver_stop_captive_dns() { stop_captive_dns_server(); }

} // namespace lunaredu::lunaros
