#include "lunaros_network_scan.h"
#include "lunaros_network_wifi_configuration.h"
#include "projection.h"
#include "device_api/device_operation_builders.h"
#include "kernel/service_executor.h"
#include "device_api/device_api_context.h"
#include "lunaros_network.h"
#include "lunaros_network_local_gatt.h"
#include "lunaros_provisioning.h"
#include <cstring>
namespace lunaredu::lunaros {
namespace {
void status(DeviceApiContext &, JsonObject payload) {
  const auto setup = provisioning_snapshot();
  const auto network = network_snapshot();
  const auto saved = network_saved_wifi_snapshot();
  const bool connected =
      network.link == NetworkLinkState::Up && !network.local_ip.empty();
  payload["connected"] = connected;
  payload["lifecycle"] = provisioning_state_name(setup.state);
  payload["ip"] = connected ? network.local_ip : setup.ip;
  payload["sta_ssid"] = network.target_ssid;
  payload["wifi_status_code"] = network.status_code;
  payload["wifi_last_disconnect_reason"] = network.last_disconnect_reason;
  payload["wifi_reconnect_attempts"] = network.reconnect_attempts;
  payload["wifi_disconnect_started_at_ms"] = network.disconnect_started_at_ms;
  payload["wifi_next_reconnect_delay_ms"] = network.next_reconnect_delay_ms;
  payload["wifi_rssi"] = network.rssi;
  payload["wifi_channel"] = network.channel;
  payload["wifi_station_suspended"] = network.station_suspended;
  payload["wifi_configured"] = saved.configured;
  payload["phase"] = setup.progress.phase;
  payload["submitted_ssid"] = setup.progress.ssid;
  payload["attempt"] = setup.progress.attempt;
  payload["error"] = setup.progress.error.empty() ? setup.error : setup.progress.error;
  payload["result_ip"] = setup.progress.ip;
  payload["supported"] = setup.enabled;
  payload["provisioning_active"] = setup.state != ProvisioningState::Idle;
  payload["remaining_ms"] = setup.remaining_ms;
  payload["softap_active"] = network.softap_active;
  payload["ap_ip"] = setup.ip;
  payload["ap_ssid"] = setup.ssid;
  payload["policy_enabled"] = setup.enabled;
  payload["policy_mode"] = provisioning_mode_name(setup.mode);
  payload["suspend_station"] = setup.suspends_station;
  payload["ble_active"] = network_local_gatt_running();
  payload["ble_error"] = network_local_gatt_snapshot().error;
  payload["bluetooth_available"] = network.bluetooth_available;
  payload["bluetooth_enabled"] = network.bluetooth_enabled;
  payload["bluetooth_connected"] = network.bluetooth_connected;
  auto wifi = payload["saved_wifi"].to<JsonObject>();
  wifi["configured"] = saved.configured;
  wifi["credential_count"] = saved.count;
  wifi["max_credentials"] = saved.credentials.size();
  wifi["last_success_index"] = saved.last_success_index;
  wifi["fixed_credentials"] = saved.fixed;
  auto credentials = wifi["credentials"].to<JsonArray>();
  for (size_t i = 0; i < saved.count; ++i) {
    auto c = credentials.add<JsonObject>();
    c["ssid"] = saved.credentials[i].ssid;
    c["password"] = "";
    c["password_masked"] = saved.credentials[i].secure;
    c["password_set"] = saved.credentials[i].secure;
  }
}
DeviceOperationResult begin(DeviceApiContext &device, JsonVariantConst input,
                            JsonObject payload) {
  std::string error;
  const auto state = provisioning_snapshot();
  auto mode=state.mode;
  if(!input["mode"].isNull()) {
    if(!input["mode"].is<const char *>() || input["mode"].as<JsonString>().size()!=6 || std::strcmp(input["mode"].as<const char *>(),"softap"))
      return device_operation_error("invalid_provisioning_mode",400);
    mode=ProvisioningMode::SoftAp;
  }
  if(!input["suspend_station"].isNull()&&!input["suspend_station"].is<bool>())
    return device_operation_error("invalid_suspend_station",400);
  if (!request_provisioning_begin(device.runtime, mode, input["suspend_station"] | false, error))
    return device_operation_error(error.c_str(), 503, "provisioning_error");
  status(device, payload);
  return device_operation_ack();
}
DeviceOperationResult end(DeviceApiContext &device, JsonVariantConst,
                          JsonObject payload) {
  std::string error;
  if (!request_provisioning_end(device.runtime, error))
    return device_operation_error(error.c_str(), 503, "provisioning_error");
  status(device, payload);
  return device_operation_ack();
}
DeviceOperationResult cancel(DeviceApiContext &device, JsonVariantConst input,
                             JsonObject payload) {
  if (!input["attempt"].is<uint32_t>())
    return device_operation_error("wifi_attempt_required", 400,
                                  "provisioning_error");
  std::string error;
  if (!request_provisioning_cancel_attempt(device.runtime, input["attempt"],
                                           error))
    return device_operation_error(error.c_str(), 409, "provisioning_error");
  status(device, payload);
  return device_operation_ack();
}
DeviceOperationResult submit(DeviceApiContext &device, JsonVariantConst input,
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
                                  "provisioning_error");
  const auto submitted =
      use_saved ? submit_provisioning_saved_wifi(device.runtime, input["ssid"])
                : submit_provisioning_wifi_credentials(
                      device.runtime, input["ssid"], input["password"]);
  if (submitted != NetworkWifiCredentialResult::Ok)
    return device_operation_error(
        submitted == NetworkWifiCredentialResult::Busy ? "provisioning_busy"
        : submitted == NetworkWifiCredentialResult::InvalidCredentials
            ? "wifi_credentials_invalid"
            : "wifi_apply_failed",
        submitted == NetworkWifiCredentialResult::Busy ? 409 : 400,
        "provisioning_error");
  payload["result"] = "accepted";
  payload["submitted_ssid"] = input["ssid"];
  payload["restart_scheduled"] = false;
  status(device, payload["status"].to<JsonObject>());
  return device_operation_ack();
}
DeviceOperationResult scan(DeviceApiContext &device, JsonVariantConst,
                           JsonObject payload) {
  NetworkWifiScanSnapshot scan;
  std::string error;
  if (!request_network_wifi_scan_and_wait(device.runtime, scan, error))
    return device_operation_error(error.c_str(),
                                  error == "wifi_scan_timeout" ? 408 : 503,
                                  "provisioning_error");
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
  status(device, payload["status"].to<JsonObject>());
  return device_operation_ack();
}
const DeviceOperationDefinition operations[] = {
    make_device_json_provider("status", status, kNetworkServiceExecutor),
    make_device_json_command(
        "start", begin, device_operation_on_executor(kNetworkServiceExecutor)),
    make_device_json_command(
        "stop", end, device_operation_on_executor(kNetworkServiceExecutor)),
    make_device_json_command(
        "submit", submit,
        device_operation_on_executor(kNetworkServiceExecutor)),
    make_device_json_command(
        "cancel", cancel,
        device_operation_on_executor(kNetworkServiceExecutor)),
    device_operation_on_caller(make_device_json_command("scan", scan)),
};
const DeviceModuleProjectionMetadata metadata{"provisioning", "provisioning"};
const DeviceModuleProjection projection{"provisioning", &metadata, operations,
                                        std::size(operations)};
const char *tags[] = {"wifi_provisioning"};
const DeviceCapabilityDescriptor descriptor = [] {
  DeviceCapabilityDescriptor d;
  d.id = "provisioning";
  d.presence_capabilities = tags;
  d.presence_capability_count = std::size(tags);
  return d;
}();
} // namespace
const DeviceCapabilityDescriptor &provisioning_capability_descriptor() {
  return descriptor;
}
bool declare_provisioning_projection(DeviceProjectionRegistry &registry,
                                     std::string &error) {
  if (!registry.register_projection({&descriptor, &projection, nullptr, false},
                                    &error))
    return false;
  (void)registry.context().capabilities.deactivate(descriptor.id);
  return true;
}
} // namespace lunaredu::lunaros
