// Device API projection for provisioning state and commands.
#include "device_api/projections/network_projection_internal.h"
#include "device_api/projections/wifi_config_payload.h"
#include <string>

#include "sdkconfig.h"

#include "lunaredu_lunaros/device_api/device_projection_json.h"
#include "lunaredu_lunaros/network/network.h"
#include "lunaredu_lunaros/network/network_commands.h"
#include "lunaredu_lunaros/network/network_provisioning.h"
#include "lunaredu_lunaros/persistence/settings.h"
#include "lunaredu_lunaros/kernel/text.h"

namespace lunaredu::lunaros {

using lunaredu::lunaros::NetworkProvisioningPolicy;

namespace {

#if CONFIG_LUNAREDU_NETWORK_ENABLE_PROVISIONING_SERVICE
NetworkProvisioningPolicy provisioning_policy() {
  return lunaredu::lunaros::network_policy().provisioning;
}
#endif

} // namespace

void fill_network_provisioning_status_payload(JsonObject payload) {
  const DeviceConfig config = device_config();
  const auto network = lunaredu::lunaros::network_snapshot();
  const ProvisioningWifiSnapshot wifi = provisioning_driver_wifi_snapshot();
  const bool connected =
      network.link == lunaredu::lunaros::NetworkLinkState::Up;
  payload["connected"] = connected;
  payload["ip"] = connected ? network.local_ip : network.provisioning_ip;
  payload["sta_ssid"] = network.target_ssid;
  payload["wifi_status_code"] = wifi.status_code;
  payload["wifi_last_disconnect_reason"] = wifi.last_disconnect_reason;
  payload["wifi_reconnect_attempts"] = wifi.reconnect_attempts;
  payload["wifi_disconnect_started_at_ms"] = wifi.disconnect_started_at_ms;
  payload["wifi_next_reconnect_delay_ms"] = wifi.next_reconnect_delay_ms;
  payload["wifi_rssi"] =
      wifi.connected ? provisioning_driver_wifi_rssi() : wifi.rssi;
  payload["wifi_channel"] =
      wifi.connected ? provisioning_driver_wifi_channel() : 0;
  payload["wifi_station_suspended"] = wifi.station_suspended;
  payload["wifi_configured"] = config.wifi.configured;

#if CONFIG_LUNAREDU_NETWORK_ENABLE_PROVISIONING_SERVICE
  const bool softap_active = network_provisioning_softap_active();
  payload["provisioning_active"] =
      network.provisioning !=
      lunaredu::lunaros::NetworkProvisioningState::Idle;
  payload["softap_active"] = softap_active;
  payload["ap_ip"] = softap_active ? wifi.softap_ip : "";
  payload["ap_ssid"] = network.provisioning_ssid;
  payload["device_uuid"] = config.identity.local_device_uuid;
  payload["device_name"] = config.identity.name;
  payload["provision_ap_name"] = config.identity.provision_ap_name;
  fill_saved_wifi_config_payload(
      payload["saved_wifi"].to<JsonObject>(), config.wifi, true);
  const NetworkProvisioningPolicy policy = provisioning_policy();
  payload["policy_enabled"] = policy.enabled;
  payload["policy_mode"] = network_provisioning_mode_name(policy.mode);
  payload["suspend_station"] = policy.suspend_station_while_provisioning;
#else
  payload["supported"] = false;
  payload["provisioning_active"] = false;
  payload["softap_active"] = false;
  payload["ap_ip"] = "";
  payload["ap_ssid"] = "";
  fill_saved_wifi_config_payload(payload["saved_wifi"].to<JsonObject>(), config.wifi);
  payload["policy_enabled"] = false;
  payload["policy_mode"] = "disabled";
  payload["suspend_station"] = false;
#endif
}

DeviceOperationResult handle_network_provision_submit(
    JsonVariantConst request_payload, JsonObject response_payload) {
  const std::string ssid = text::to_std_string(request_payload["ssid"] | "");
  const std::string password =
      text::to_std_string(request_payload["password"] | "");
  const NetworkProvisioningCredentialResult result =
      submit_network_provisioning_wifi_credentials(ssid.c_str(),
                                                   password.c_str());

  switch (result) {
  case NetworkProvisioningCredentialResult::Ok: {
    response_payload["result"] = "ok";
    response_payload["submitted_ssid"] = ssid;
#if CONFIG_LUNAREDU_NETWORK_ENABLE_PROVISIONING_SERVICE
    const NetworkProvisioningPolicy policy = provisioning_policy();
    if (policy.enabled && policy.restart_after_submit_success) {
      response_payload["restart_scheduled"] = true;
      response_payload["restart_delay_ms"] =
          schedule_network_provisioning_restart();
    } else {
      response_payload["restart_scheduled"] = false;
    }
#else
    response_payload["restart_scheduled"] = false;
#endif
    fill_network_provisioning_status_payload(
        response_payload["status"].to<JsonObject>());
    return device_operation_ack();
  }
  case NetworkProvisioningCredentialResult::MissingSsid:
    return device_operation_error("missing_ssid", 400, "network_provision_error");
  case NetworkProvisioningCredentialResult::NotReady:
    return device_operation_error("provision_not_ready", 503,
                           "network_provision_error");
  case NetworkProvisioningCredentialResult::SaveFailed:
    return device_operation_error("save_failed", 500, "network_provision_error");
  case NetworkProvisioningCredentialResult::ApplyFailed:
    return device_operation_error("apply_failed", 500, "network_provision_error");
  }

  return device_operation_error("submit_failed", 500, "network_provision_error");
}

DeviceOperationResult handle_network_provision_scan(
    JsonVariantConst request_payload, JsonObject response_payload) {
  (void)request_payload;
#if !CONFIG_LUNAREDU_NETWORK_ENABLE_PROVISIONING_SERVICE
  (void)response_payload;
  return device_operation_error("provision_disabled", 503,
                         "network_provision_error");
#else
  NetworkWifiScanSnapshot scan;
  std::string error;
  if (!request_network_wifi_scan_and_wait(scan, error)) {
    return device_operation_error(
        error.empty() ? "wifi_scan_failed" : error.c_str(),
        error == "wifi_scan_timeout" ? 408 : 503,
        "network_provision_error");
  }

  JsonArray aps = response_payload["aps"].to<JsonArray>();
  for (size_t index = 0; index < scan.access_point_count; ++index) {
    const auto &record = scan.access_points[index];
    JsonObject item = aps.add<JsonObject>();
    item["ssid"] = record.ssid.data();
    item["rssi"] = record.rssi;
    item["authmode"] = record.authmode;
    item["channel"] = record.channel;
    item["secure"] = record.secure;
  }
  response_payload["count"] = scan.access_point_count;
  fill_network_provisioning_status_payload(
      response_payload["status"].to<JsonObject>());
  return device_operation_ack();
#endif
}

} // namespace lunaredu::lunaros
