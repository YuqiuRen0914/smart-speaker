#include "lunaredu_lunaros/device_link/device_link_json.h"
#include "device_link/device_link_operations.h"

#include <ArduinoJson.h>

#include <cstdio>

#include "lunaredu_lunaros/kernel/log.h"
#include "lunaredu_lunaros/persistence/identity.h"
#include "lunaredu_lunaros/delivery/update.h"
#include "device_link/device_link_http.h"
#include "device_link/device_link_state.h"

namespace lunaredu::lunaros::device_link {
namespace {

constexpr const char *kTag = "device_link";

void cache_available_firmware(JsonVariantConst firmware_update) {
  OtaAvailableUpdate available_update;
  if (!firmware_update.isNull() && (firmware_update["has_update"] | false)) {
    const JsonVariantConst release = firmware_update["release"];
    const JsonVariantConst ui = release["ota_manifest"]["ui"];
    const std::string policy = firmware_update["policy"] | "";
    available_update.available = true;
    available_update.mandatory =
        policy == "mandatory" || (ui["mandatory"] | false);
    available_update.version =
        firmware_update["latest_version"] | "";
    available_update.summary = ui["summary"] | "";
    available_update.changelog = ui["changelog"] | "";
    available_update.download_url = firmware_update["download_url"] | "";
    available_update.package_sha256 = firmware_update["package_sha256"] | "";
    available_update.package_size =
        firmware_update["package_size"] | static_cast<size_t>(0);
    if (available_update.download_url.empty() ||
        available_update.package_sha256.empty() ||
        available_update.package_size == 0) {
      available_update.available = false;
    }
  }
  ota_service_set_available_update(available_update);
}

} // namespace

bool publish_presence() {
  configure_resource_transport();

  JsonDocument doc;
  doc["firmwareVersion"] = firmware_version();
  add_capabilities(doc["capabilities"].to<JsonArray>());
  add_runtime_state(doc["state"].to<JsonObject>());
  std::string body;
  serializeJson(doc, body);

  JsonDocument response;
  int status_code = 0;
  if (!perform_request("POST", "/api/link/device-runtime/presence", &body,
                       &response, &status_code)) {
    if (status_code == 401 &&
        pairing_snapshot().state != DeviceLinkBindingState::Pairing) {
      update_pairing_snapshot(DeviceLinkBindingState::Error, {}, {}, 0,
                              "device_credential_rejected");
    }
    return false;
  }
  if (response["data"]["projectionPending"] | false) {
    const auto binding = pairing_snapshot();
    return (response["data"]["accepted"] | false) &&
        response["data"]["presenceReceiptVersion"] == 1 &&
        response["data"]["deviceStatus"] == "active" &&
        binding.state == DeviceLinkBindingState::Bound &&
        response["data"]["bindingEpoch"] == binding.binding_epoch;
  }
  const int binding_epoch = response["data"]["bindingEpoch"] | 0;
  const std::string device_status = response["data"]["deviceStatus"] | "";
  if (!response["data"]["bindingEpoch"].is<int>() || binding_epoch < 0 ||
      state_from_server(device_status) == DeviceLinkBindingState::Error ||
      (device_status == "active" && (binding_epoch <= 0 ||
       (response["data"]["boundAccount"]["id"] | std::string{}).empty()))) {
    update_pairing_snapshot(DeviceLinkBindingState::Error, {}, {}, 0, "platform_binding_response_invalid");
    return false;
  }
  if (device_status == "active") {
    const std::string account_id =
        response["data"]["boundAccount"]["id"] | "";
    const std::string account_display_name =
        response["data"]["boundAccount"]["displayName"] | "";
    const std::string account_avatar_url =
        response["data"]["boundAccount"]["avatarUrl"] | "";
    const bool account_can_manage_hardware =
        response["data"]["boundAccount"]["canManageHardware"] | false;
    update_pairing_snapshot(DeviceLinkBindingState::Bound, {}, {}, 0, {},
                            binding_epoch, account_id, account_display_name,
                            account_avatar_url, account_can_manage_hardware);
  } else {
    const DeviceLinkPairingSnapshot current = pairing_snapshot();
    if (current.state != DeviceLinkBindingState::Pairing) {
      update_pairing_snapshot(state_from_server(device_status), {}, {}, 0, {},
                              binding_epoch);
    }
  }

  const JsonVariantConst firmware_update = response["data"]["firmwareUpdate"];
  cache_available_firmware(firmware_update);
  persist_cached_binding();
  const auto *observer = runtime_config().observer;
  if (observer && observer->presence) {
    observer->presence(pairing_snapshot(), response["data"]);
  }
  LOGI(kTag, "account presence accepted: serial=%s binding_epoch=%d",
       hardware_serial_number(), binding_epoch);
  return true;
}

bool refresh_and_apply_firmware_update() {
  JsonDocument response;
  int status_code = 0;
  if (!perform_request("GET", "/api/link/device-runtime/firmware", nullptr,
                       &response, &status_code)) {
    char failure[64];
    std::snprintf(failure, sizeof(failure), "ota_ticket_refresh_failed:%d",
                  status_code);
    ota_service_report_failure(failure);
    LOGW(kTag, "firmware ticket refresh failed status=%d", status_code);
    return false;
  }
  cache_available_firmware(response["data"]);
  std::string error;
  if (!ota_service_apply_available_update(error)) {
    LOGW(kTag, "fresh official OTA update rejected error=%s", error.c_str());
    return false;
  }
  LOGI(kTag, "fresh official OTA ticket accepted");
  return true;
}

} // namespace lunaredu::lunaros::device_link
