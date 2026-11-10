#include "lunaros_network.h"
#include "lunaros_network_scan.h"
#include "lunaros_network_control.h"
#include "lunaros_network_wifi_configuration.h"
#include "projections/network_projection_internal.h"
#include <cstring>
namespace lunaredu::lunaros {
void fill_network_wifi_status(DeviceApiContext &, JsonObject payload) {
  const auto network = network_snapshot();
  const auto connection = network_wifi_connection_snapshot();
  const auto saved = network_saved_wifi_snapshot();
  payload["connected"] =
      network.link == NetworkLinkState::Up && !network.local_ip.empty();
  payload["sta_ssid"] = network.target_ssid;
  payload["ip"] = network.local_ip;
  payload["wifi_rssi"] = network.rssi;
  payload["wifi_channel"] = network.channel;
  payload["wifi_last_disconnect_reason"] = network.last_disconnect_reason;
  payload["wifi_reconnect_attempts"] = network.reconnect_attempts;
  payload["softap_active"] = network.softap_active;
  payload["ap_ip"] = network.local_access_ip;
  payload["wifi_station_suspended"] = network.station_suspended;
  payload["phase"] = network_wifi_connection_phase_name(connection.phase);
  payload["attempt"] = connection.attempt;
  payload["submitted_ssid"] = connection.ssid;
  payload["result_ip"] = connection.ip;
  payload["error"] = connection.error;
  payload["bluetooth_available"] = network.bluetooth_available;
  const auto bluetooth = network_bluetooth_control_snapshot();
  payload["bluetooth_enabled"] = bluetooth.enabled;
  payload["bluetooth_stop_pending"] = bluetooth.stop_pending;
  payload["bluetooth_error"] = bluetooth.error;
  payload["bluetooth_connected"] = network.bluetooth_connected;
  auto wifi = payload["saved_wifi"].to<JsonObject>();
  wifi["configured"] = saved.configured;
  wifi["credential_count"] = saved.count;
  wifi["max_credentials"] = saved.credentials.size();
  wifi["last_success_index"] = saved.last_success_index;
  wifi["fixed_credentials"] = saved.fixed;
  auto credentials = wifi["credentials"].to<JsonArray>();
  for (size_t i = 0; i < saved.count; ++i) {
    auto item = credentials.add<JsonObject>();
    item["ssid"] = saved.credentials[i].ssid;
    item["password"] = "";
    item["password_set"] = saved.credentials[i].secure;
    item["password_masked"] = saved.credentials[i].secure;
  }
}
DeviceOperationResult handle_network_wifi_connect(DeviceApiContext &device,
                                                  JsonVariantConst input,
                                                  JsonObject payload) {
  const bool use_saved = input["use_saved"] | false;
  if (!input["ssid"].is<const char *>() ||
      input["ssid"].as<JsonString>().size() !=
          std::strlen(input["ssid"].as<const char *>()) ||
      (!input["use_saved"].isNull() && !input["use_saved"].is<bool>()) ||
      (!use_saved && (!input["password"].is<const char *>() ||
                      input["password"].as<JsonString>().size() !=
                          std::strlen(input["password"].as<const char *>()))))
    return device_operation_error("wifi_credentials_invalid", 400,
                                  "network_config_error");
  uint32_t attempt = 0;
  const auto result =
      use_saved ? request_network_connect_saved_wifi(device.runtime,
                                                     input["ssid"], attempt)
                : request_network_connect_wifi(device.runtime, input["ssid"],
                                               input["password"], attempt);
  if (result != NetworkWifiCredentialResult::Ok)
    return device_operation_error(
        result == NetworkWifiCredentialResult::Busy ? "wifi_configuration_busy"
        : result == NetworkWifiCredentialResult::InvalidCredentials
            ? "wifi_credentials_invalid"
            : "wifi_apply_failed",
        result == NetworkWifiCredentialResult::Busy ? 409 : 400,
        "network_config_error");
  payload["result"] = "accepted";
  payload["attempt"] = attempt;
  payload["submitted_ssid"] = input["ssid"];
  payload["restart_scheduled"] = false;
  fill_network_wifi_status(device, payload["status"].to<JsonObject>());
  return device_operation_ack();
}
DeviceOperationResult handle_network_wifi_cancel(DeviceApiContext &device,
                                                 JsonVariantConst input,
                                                 JsonObject payload) {
  if (!input["attempt"].is<uint32_t>())
    return device_operation_error("wifi_attempt_required", 400,
                                  "network_config_error");
  std::string error;
  if (!request_network_cancel_wifi_connection(device.runtime, input["attempt"],
                                              error))
    return device_operation_error(error.c_str(), 409, "network_config_error");
  fill_network_wifi_status(device, payload);
  return device_operation_ack();
}
DeviceOperationResult handle_network_wifi_forget(DeviceApiContext &device,
                                                 JsonVariantConst input,
                                                 JsonObject payload) {
  if (!input["ssid"].is<const char *>() ||
      input["ssid"].as<JsonString>().size() !=
          std::strlen(input["ssid"].as<const char *>()))
    return device_operation_error("missing_ssid", 400, "network_config_error");
  std::string error;
  if (!request_network_forget_wifi(device.runtime, input["ssid"], error))
    return device_operation_error(error.c_str(), 409, "network_config_error");
  fill_network_config_payload(device, payload);
  payload["restart_scheduled"] = false;
  return device_operation_ack();
}
DeviceOperationResult handle_network_wifi_scan(DeviceApiContext &device,
                                               JsonVariantConst,
                                               JsonObject payload) {
  NetworkWifiScanSnapshot scan;
  std::string error;
  if (!request_network_wifi_scan_and_wait(device.runtime, scan, error))
    return device_operation_error(error.c_str(),
                                  error == "wifi_scan_timeout" ? 408 : 503,
                                  "network_config_error");
  auto aps = payload["aps"].to<JsonArray>();
  for (size_t i = 0; i < scan.access_point_count; ++i) {
    const auto &ap = scan.access_points[i];
    auto item = aps.add<JsonObject>();
    item["ssid"] = ap.ssid.data();
    item["rssi"] = ap.rssi;
    item["authmode"] = ap.authmode;
    item["channel"] = ap.channel;
    item["secure"] = ap.secure;
  }
  payload["count"] = scan.access_point_count;
  return device_operation_ack();
}
} // namespace lunaredu::lunaros
