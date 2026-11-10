#include "lunaros_network_control.h"
#include "lunaros_network_wifi_configuration.h"
#include "lunaros_network_lifecycle.h"
#include "device_api/device_api_context.h"
#include "projections/network_projection_internal.h"
#include "network/wifi/wifi_configuration_internal.h"
#include "configuration/wifi_credentials.h"
#include "network/wifi/wifi_manager.h"
#include "sdkconfig.h"
#include <cstring>
#include <optional>
#include "device_api/adapters/identity_payload.h"
#include "projections/wifi_config_payload.h"
#include "device_api/device_operation_types.h"
#include "configuration/device_config.h"
#include "system/identity.h"
#include "platform/text.h"
#include "lunaros_network.h"

namespace lunaredu::lunaros {

namespace {

std::optional<std::string> stored_password_for_ssid(const DeviceWifiConfig &wifi,
                                     const std::string &ssid) {
  for (uint8_t index = 0; index < wifi.credential_count && index < kDeviceWifiCredentialMax; ++index) {
    if (wifi.credentials[index].ssid == ssid) {
      return wifi.credentials[index].password;
    }
  }
  return std::nullopt;
}

size_t read_wifi_credentials(JsonVariantConst source,
                             DeviceWifiCredential *credentials) {
  JsonArrayConst array = source["credentials"].as<JsonArrayConst>();

  size_t count = 0;
  if (!array.isNull()) {
    std::optional<DeviceWifiConfig> saved_config;
    for (JsonObjectConst item : array) {
      if (count >= kDeviceWifiCredentialMax) {
        break;
      }

      if (!item["ssid"].is<const char *>() ||
          (!item["password_masked"].isNull() && !item["password_masked"].is<bool>()) ||
          (!item["password"].isNull() && !item["password"].is<const char *>())) continue;
      const auto raw_ssid = item["ssid"].as<JsonString>();
      const std::string ssid(raw_ssid.c_str(), raw_ssid.size());
      if (ssid.empty()) {
        continue;
      }

      credentials[count].ssid = ssid;
      const bool preserve_masked_password =
          static_cast<bool>(item["password_masked"] | false);
      if (preserve_masked_password) {
        if (!saved_config) saved_config.emplace(device_wifi_config());
        const auto password = stored_password_for_ssid(*saved_config, ssid);
        if (!password) continue; // A mask cannot create an unknown open network.
        credentials[count].password = *password;
      } else {
        const auto password = item["password"].as<JsonString>();
        credentials[count].password = password.isNull() ? "" :
            std::string(password.c_str(), password.size());
      }
      ++count;
    }
  }

  return count;
}

} // namespace

void fill_network_service_status_payload([[maybe_unused]] DeviceApiContext &device, JsonObject payload) {
  const auto network = lunaredu::lunaros::network_snapshot();
  JsonObject wifi = payload["wifi"].to<JsonObject>();
  wifi["configured"] = device_wifi_configured();
  wifi["connected"] = network.link == lunaredu::lunaros::NetworkLinkState::Up;
  wifi["ssid"] = network.target_ssid;
  wifi["local_ip"] = network.local_ip;
  wifi["status_code"] = network.status_code;
  wifi["last_disconnect_reason"] = network.last_disconnect_reason;
  wifi["rssi"] = network.rssi;
  wifi["channel"] = network.channel;
  wifi["station_suspended"] = network.station_suspended;
  wifi["softap_active"] = network.softap_active;
  wifi["local_access_active"] = network.local_access != NetworkLocalAccessState::Idle;
  JsonObject events = wifi["events"].to<JsonObject>();
  events["capacity"] = kNetworkWifiEventQueueCapacity;
  events["queued"] = network.wifi_events.queued;
  events["high_water"] = network.wifi_events.high_water;
  events["enqueued"] = network.wifi_events.enqueued;
  events["processed"] = network.wifi_events.processed;
  events["overflow_dropped"] = network.wifi_events.overflow_dropped;
  events["cleared"] = network.wifi_events.cleared;
}

void fill_network_service_bootstrap_payload([[maybe_unused]] DeviceApiContext &device, JsonObject payload) {
  const auto network = lunaredu::lunaros::network_snapshot();
  payload["wifi_connected"] =
      network.link == lunaredu::lunaros::NetworkLinkState::Up;
  payload["wifi_ssid"] = network.target_ssid;
  payload["wifi_local_ip"] = network.local_ip;
  payload["wifi_local_access_active"] =
      network.local_access !=
      lunaredu::lunaros::NetworkLocalAccessState::Idle;
}

void fill_network_config_payload([[maybe_unused]] DeviceApiContext &device, JsonObject payload) {
  payload["ok"] = true;
  const DeviceNetworkConfig config = device_network_config();
  fill_device_identity_payload(payload["identity"].to<JsonObject>(), config.identity);
  JsonObject wifi = payload["wifi"].to<JsonObject>();
  fill_saved_wifi_config_payload(wifi, config.wifi);
  const auto network = lunaredu::lunaros::network_snapshot();
  wifi["fixed_credentials"] = network.fixed_wifi_credentials;
  wifi["connected"] =
      network.link == lunaredu::lunaros::NetworkLinkState::Up;
  wifi["current_ssid"] = network.target_ssid;
  wifi["local_ip"] = network.local_ip;
}

DeviceOperationResult handle_network_config_set([[maybe_unused]] DeviceApiContext &device, JsonVariantConst request_payload, JsonObject response_payload) {
  if (network_fast_snapshot().fixed_wifi_credentials) {
    return device_operation_error("fixed_wifi_credentials", 403, "network_config_error");
  }
  if (!network_runtime_owned_by(device.runtime)) {
    return device_operation_error("network_runtime_owner_mismatch", 409, "network_config_error");
  }
  if (wifi_configuration_busy() || wifi_runtime_snapshot().apply_pending) {
    return device_operation_error("wifi_configuration_busy", 409, "network_config_error");
  }
  DeviceWifiCredential credentials[kDeviceWifiCredentialMax];
  const size_t credential_count =
      read_wifi_credentials(request_payload, credentials);
  const JsonArrayConst requested_credentials = request_payload["credentials"].as<JsonArrayConst>();
  if (requested_credentials.isNull() || requested_credentials.size() > kDeviceWifiCredentialMax ||
      requested_credentials.size() != credential_count) {
    return device_operation_error("invalid_wifi_credentials", 400, "network_config_error");
  }
  if (credential_count == 0) {
    return device_operation_error("missing_ssid", 400,
                                     "network_config_error");
  }
  for (size_t index = 0; index < credential_count; ++index) {
    if (!valid_device_wifi_credentials(credentials[index].ssid, credentials[index].password))
      return device_operation_error("invalid_wifi_credentials", 400, "network_config_error");
    for (size_t earlier = 0; earlier < index; ++earlier)
      if (credentials[earlier].ssid == credentials[index].ssid)
        return device_operation_error("duplicate_wifi_ssid", 400, "network_config_error");
  }
  if ((!request_payload["apply"].isNull() && !request_payload["apply"].is<bool>()) ||
      (!request_payload["apply_index"].isNull() && !request_payload["apply_index"].is<size_t>()))
    return device_operation_error("invalid_wifi_apply", 400, "network_config_error");
  const bool apply = request_payload["apply"] | true;
  const size_t apply_index = request_payload["apply_index"] | size_t{0};
  if (apply_index >= credential_count)
    return device_operation_error("invalid_wifi_apply_index", 400, "network_config_error");

  for (const char *key : {"device_name", "provision_ap_name"}) {
    const JsonVariantConst value = request_payload[key];
    if (value.isNull()) continue;
    if (!value.is<const char *>() ||
        value.as<JsonString>().size() != std::strlen(value.as<const char *>()))
      return device_operation_error("invalid_network_identity", 400, "network_config_error");
  }

  const std::string device_name =
      text::to_std_string(request_payload["device_name"] | "");
  const std::string provision_ap_name =
      text::to_std_string(request_payload["provision_ap_name"] | "");
  std::string apply_error;
  if (!set_network_wifi_config_on_owner(credentials, credential_count,
          device_name.c_str(), provision_ap_name.c_str(), apply,
          static_cast<uint8_t>(apply_index), apply_error)) {
    const int status = apply_error == "wifi_configuration_busy" ||
        apply_error == "network_station_unavailable" ? 409 :
        apply_error == "fixed_wifi_credentials" ? 403 : 500;
    return device_operation_error(apply_error.c_str(), status, "network_config_error");
  }

  fill_network_config_payload(device, response_payload);
  response_payload["apply_requested"] = apply;
  return device_operation_ack();
}

DeviceOperationResult handle_network_config_clear([[maybe_unused]] DeviceApiContext &device, JsonVariantConst request_payload, JsonObject response_payload) {
  if (network_fast_snapshot().fixed_wifi_credentials) {
    return device_operation_error("fixed_wifi_credentials", 403, "network_config_error");
  }
  if (!network_runtime_owned_by(device.runtime)) {
    return device_operation_error("network_runtime_owner_mismatch", 409, "network_config_error");
  }
  (void)request_payload;
  std::string error;
  if(!request_network_clear_saved_wifi(device.runtime,error))return device_operation_error(error.c_str(),500,"network_config_error");
  response_payload["result"]="cleared";response_payload["restart_scheduled"]=false;
  fill_network_config_payload(device, response_payload);
  return device_operation_ack();
}

} // namespace lunaredu::lunaros
