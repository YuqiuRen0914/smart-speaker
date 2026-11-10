#include "device_api/projections/network_projection_internal.h"
#include "sdkconfig.h"
#include <optional>
#include "device_api/projections/identity_projection_internal.h"
#include "device_api/projections/wifi_config_payload.h"
#include "lunaredu_lunaros/device_api/device_projection_json.h"
#include "lunaredu_lunaros/persistence/settings.h"
#include "lunaredu_lunaros/persistence/identity.h"
#include "lunaredu_lunaros/kernel/text.h"
#include "lunaredu_lunaros/network/network.h"
#include "lunaredu_lunaros/network/network_commands.h"
#include "lunaredu_lunaros/network/network_provisioning.h"

namespace lunaredu::lunaros {

namespace {

std::string stored_password_for_ssid(const DeviceWifiConfig &wifi,
                                     const std::string &ssid) {
  for (uint8_t index = 0; index < wifi.credential_count; ++index) {
    if (wifi.credentials[index].ssid == ssid) {
      return wifi.credentials[index].password;
    }
  }
  return "";
}

size_t read_wifi_credentials(JsonVariantConst source,
                             DeviceWifiCredential *credentials) {
  JsonArrayConst array = source["credentials"].as<JsonArrayConst>();

  size_t count = 0;
  if (!array.isNull()) {
    std::optional<DeviceConfig> saved_config;
    for (JsonObjectConst item : array) {
      if (count >= kDeviceWifiCredentialMax) {
        break;
      }

      const std::string ssid =
          text::to_std_string(item["ssid"] | "");
      if (ssid.empty()) {
        continue;
      }

      credentials[count].ssid = ssid;
      const bool preserve_masked_password =
          static_cast<bool>(item["password_masked"] | false);
      if (preserve_masked_password) {
        if (!saved_config) saved_config.emplace(device_config());
        credentials[count].password =
            stored_password_for_ssid(saved_config->wifi, ssid);
      } else {
        credentials[count].password = text::to_std_string(item["password"] | "");
      }
      ++count;
    }
  }

  return count;
}

} // namespace

void fill_network_service_status_payload(JsonObject payload) {
  const auto network = lunaredu::lunaros::network_snapshot();
  const ProvisioningWifiSnapshot diagnostics =
      provisioning_driver_wifi_snapshot();
  JsonObject wifi = payload["wifi"].to<JsonObject>();
  wifi["configured"] = device_config().wifi.configured;
  wifi["connected"] = network.link == lunaredu::lunaros::NetworkLinkState::Up;
  wifi["ssid"] = network.target_ssid;
  wifi["local_ip"] = network.local_ip;
  wifi["status_code"] = diagnostics.status_code;
  wifi["last_disconnect_reason"] = diagnostics.last_disconnect_reason;
  wifi["rssi"] = diagnostics.connected ? provisioning_driver_wifi_rssi()
                                       : diagnostics.rssi;
  wifi["channel"] =
      diagnostics.connected ? provisioning_driver_wifi_channel() : 0;
  wifi["station_suspended"] = diagnostics.station_suspended;
  wifi["softap_active"] = diagnostics.softap_active;
  wifi["provision_active"] = network.provisioning != NetworkProvisioningState::Idle;
}

void fill_network_service_bootstrap_payload(JsonObject payload) {
  const auto network = lunaredu::lunaros::network_snapshot();
  payload["wifi_connected"] =
      network.link == lunaredu::lunaros::NetworkLinkState::Up;
  payload["wifi_ssid"] = network.target_ssid;
  payload["wifi_local_ip"] = network.local_ip;
  payload["wifi_provision_active"] =
      network.provisioning !=
      lunaredu::lunaros::NetworkProvisioningState::Idle;
}

void fill_network_config_payload(JsonObject payload) {
  payload["ok"] = true;
  const DeviceConfig config = device_config();
  fill_device_identity_payload(payload["identity"].to<JsonObject>(), config.identity);
  JsonObject wifi = payload["wifi"].to<JsonObject>();
  fill_saved_wifi_config_payload(wifi, config.wifi);
  const auto network = lunaredu::lunaros::network_snapshot();
  wifi["connected"] =
      network.link == lunaredu::lunaros::NetworkLinkState::Up;
  wifi["current_ssid"] = network.target_ssid;
  wifi["local_ip"] = network.local_ip;
}

DeviceOperationResult handle_network_config_set(
    JsonVariantConst request_payload, JsonObject response_payload) {
  DeviceWifiCredential credentials[kDeviceWifiCredentialMax];
  const size_t credential_count =
      read_wifi_credentials(request_payload, credentials);
  if (credential_count == 0) {
    return device_operation_error("missing_ssid", 400,
                                     "network_config_error");
  }

  const std::string device_name =
      text::to_std_string(request_payload["device_name"] | "");
  const std::string provision_ap_name =
      text::to_std_string(request_payload["provision_ap_name"] | "");
  if (!save_device_network_config(credentials, credential_count,
                                  device_name.c_str(),
                                  provision_ap_name.c_str())) {
    return device_operation_error("save_failed", 500,
                                     "network_config_error");
  }

  std::string apply_error;
  if (!lunaredu::lunaros::request_network_apply_saved_credential(
          0, apply_error)) {
    return device_operation_error("reconfigure_failed", 500,
                                     "network_config_error");
  }

  fill_network_config_payload(response_payload);
  return device_operation_ack();
}

DeviceOperationResult handle_network_config_clear(
    JsonVariantConst request_payload, JsonObject response_payload) {
  (void)request_payload;
  if (!clear_device_wifi_credentials()) {
    return device_operation_error("clear_failed", 500,
                                  "network_config_error");
  }

  response_payload["result"] = "cleared";
#if CONFIG_LUNAREDU_NETWORK_ENABLE_PROVISIONING_SERVICE
  response_payload["restart_scheduled"] = true;
  response_payload["restart_delay_ms"] =
      schedule_network_provisioning_restart();
#else
  response_payload["restart_scheduled"] = false;
#endif
  fill_network_config_payload(response_payload);
  return device_operation_ack();
}

} // namespace lunaredu::lunaros
