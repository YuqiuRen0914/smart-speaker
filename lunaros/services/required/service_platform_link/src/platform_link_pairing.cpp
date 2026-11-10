#include "platform_link_operations.h"

#include <algorithm>
#include <ArduinoJson.h>

#include <esp_log.h>
#include "configuration/device_config.h"
#include "system/identity.h"
#include "platform_link_http.h"
#include "platform_link_environment.h"
#include "platform/system.h"
#include "platform_link_state.h"
#include "lunaros_resource.h"

namespace lunaredu::lunaros::platform_link {
namespace {

constexpr const char *kTag = "platform_link";

std::string create_pairing_code() {
  constexpr uint32_t kPairingCodeRange = 1000000;
  const uint32_t value =
      system_random_u32() % kPairingCodeRange;
  std::string code = std::to_string(value);
  code.insert(code.begin(), 6 - code.size(), '0');
  return code;
}

std::string pairing_failure_code(const JsonDocument &response,
                                 int status_code) {
  const std::string server_code = response["code"] | "";
  if (server_code == "device_already_bound" ||
      server_code == "device_retired") {
    return server_code;
  }
  if (status_code == 429) {
    return "pairing_rate_limited";
  }
  if (status_code >= 500) {
    return "pairing_service_error";
  }
  if (status_code >= 400) {
    return "pairing_request_rejected";
  }
  return "pairing_service_unreachable";
}

} // namespace

std::string create_pairing_nonce() {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string nonce(64, '0');
  for (size_t index = 0; index < nonce.size(); index += 8) {
    const uint32_t value = system_random_u32();
    for (size_t byte = 0; byte < 4; ++byte) {
      const uint8_t part = static_cast<uint8_t>(value >> (byte * 8));
      nonce[index + byte * 2] = kHex[part >> 4];
      nonce[index + byte * 2 + 1] = kHex[part & 0x0f];
    }
  }
  return nonce;
}

bool create_pairing_session() {
  // This function runs on the network worker. Admission from UI performs no
  // credential persistence, DNS, clock synchronization or HTTP work.
  if (!account_identity_ready()) {
    if(!device_cloud_config().credential.empty()) {
      update_pairing_snapshot(PlatformLinkBindingState::Error,{}, {},0,"device_credential_invalid");
      return false;
    }
    const auto credential=create_pairing_nonce();
    if (!save_device_cloud_config(platform_is_production()?"production":"development",credential.c_str())) {
      update_pairing_snapshot(PlatformLinkBindingState::Error,{}, {},0,"identity_save_failed");
      return false;
    }
  }
  std::string clock_error;
  if (!ensure_network_clock(clock_error)) {
    update_pairing_snapshot(PlatformLinkBindingState::Error, {}, {}, 0,
                            clock_error.empty() ? "time_sync_failed"
                                                : clock_error);
    return false;
  }
  if (!account_identity_ready()) {
    update_pairing_snapshot(PlatformLinkBindingState::Error, {}, {}, 0,
                            "identity_not_ready");
    return false;
  }
  const std::string nonce = create_pairing_nonce();
  const DeviceConfig &config = device_config();
  const PlatformLinkConfig &runtime = runtime_config();
  const std::string pairing_code = create_pairing_code();

  JsonDocument request;
  request["serialNumber"] = hardware_serial_number();
  request["credential"] = config.cloud.credential;
  request["pairingCode"] = pairing_code;
  request["nonce"] = nonce;
  request["displayName"] = config.identity.name;
  request["boardType"] =
      runtime.board_type.empty() ? "unknown" : runtime.board_type;
  request["model"] =
      runtime.model.empty() ? "LunarEdu ESP32 Device" : runtime.model;
  request["firmwareVersion"] = firmware_version();
  add_capabilities(request["capabilities"].to<JsonArray>());
  std::string body;
  serializeJson(request, body);
  JsonDocument response;
  int status_code = 0;
  if (!perform_public_request(
          "POST", "/api/link/device-enrollment/pairing-sessions", &body,
          &response, &status_code)) {
    update_pairing_snapshot(PlatformLinkBindingState::Error, {}, {}, 0,
                            pairing_failure_code(response, status_code));
    return false;
  }
  const std::string code = response["data"]["pairing"]["code"] | "";
  const std::string bind_path =
      response["data"]["pairing"]["bindPath"] | "";
  const int64_t expires_at_ms =
      response["data"]["pairing"]["expiresAtMs"] | 0LL;
  if (code.empty() || bind_path.empty() || expires_at_ms <= 0) {
    update_pairing_snapshot(PlatformLinkBindingState::Error, {}, {}, 0,
                            "pairing_response_invalid");
    return false;
  }
  const auto browser_origin = binding_browser_origin(platform_base_url());
  if (bind_path.front() != '/' || bind_path.rfind("//", 0) == 0) {
    update_pairing_snapshot(PlatformLinkBindingState::Error, {}, {}, 0, "pairing_response_invalid");
    return false;
  }
  update_pairing_snapshot(PlatformLinkBindingState::Pairing, code,
                          browser_origin.empty() ? "" : browser_origin + bind_path,
                          expires_at_ms);
  ESP_LOGI(kTag, "pairing session ready");
  return pairing_snapshot().state == PlatformLinkBindingState::Pairing;
}

bool poll_pairing_session() {
  JsonDocument response;
  if (!perform_request("GET",
                       "/api/link/device-runtime/pairing-sessions",
                       nullptr, &response)) {
    return false;
  }
  const std::string status = response["data"]["deviceStatus"] | "";
  const int binding_epoch = response["data"]["bindingEpoch"] | 0;
  if (!response["data"]["bindingEpoch"].is<int>() || binding_epoch<0 ||
      state_from_server(status)==PlatformLinkBindingState::Error ||
      (status=="active" && (response["data"]["boundAccount"]["id"] | std::string{}).empty())) {
    update_pairing_snapshot(PlatformLinkBindingState::Error,{}, {},0,"platform_binding_response_invalid");
    return false;
  }
  if (status == "active") {
    const std::string account_id =
        response["data"]["boundAccount"]["id"] | "";
    const std::string account_display_name =
        response["data"]["boundAccount"]["displayName"] | "";
    const std::string account_avatar_url =
        response["data"]["boundAccount"]["avatarUrl"] | "";
    const bool account_can_manage_hardware =
        response["data"]["boundAccount"]["canManageHardware"] | false;
    auto binding = pairing_snapshot();
    if (binding.binding_claim_required) {
      binding.binding_epoch = binding_epoch;
      binding.account_id = account_id;
      binding.account_display_name = account_display_name;
      binding.account_avatar_url = account_avatar_url;
      binding.account_can_manage_hardware = account_can_manage_hardware;
      std::string error;
      return complete_pairing_binding(std::move(binding), error);
    }
    update_pairing_snapshot(PlatformLinkBindingState::Bound, {}, {}, 0, {},
                            binding_epoch, account_id, account_display_name,
                            account_avatar_url, account_can_manage_hardware);
    return pairing_snapshot().state == PlatformLinkBindingState::Bound;
  }
  if (response["data"]["pairing"].isNull()) {
    update_pairing_snapshot(PlatformLinkBindingState::Unbound, {}, {}, 0,
                            {}, binding_epoch);
  }
  return pairing_snapshot().error.empty();
}

bool cancel_pairing_session() {
  if (!perform_request("DELETE",
                       "/api/link/device-runtime/pairing-sessions",
                       nullptr, nullptr)) {
    return false;
  }
  update_pairing_snapshot(PlatformLinkBindingState::Unbound);
  return pairing_snapshot().error.empty();
}

bool unbind_device() {
  auto before = pairing_snapshot();
  std::string revocation_error;
  if (before.persistence_pending && !retry_binding_persistence(revocation_error)) return false;
  before = pairing_snapshot();
  if (!before.revocation_requested) {
    if (before.state == PlatformLinkBindingState::Unbound) return true;
    if (!prepare_binding_revocation(revocation_error)) return false;
    before = pairing_snapshot();
  }
  JsonDocument request;
  request["expectedBindingEpoch"] = before.binding_epoch;
  std::string body;
  serializeJson(request, body);
  JsonDocument response;
  int status_code = 0;
  if (!perform_request("DELETE", "/api/link/device-runtime/binding/revocation",
                       &body, &response, &status_code)) return false;
  const auto data = response["data"];
  const std::string state = data["state"] | "";
  if (!data["bindingEpoch"].is<int>() ||
      !data["expectedBindingEpoch"].is<int>() ||
      data["expectedBindingEpoch"].as<int>() != before.binding_epoch ||
      (state != "unbound" && state != "superseded") ||
      data["bindingEpoch"].as<int>() < before.binding_epoch) return false;
  return complete_binding_revocation(before.binding_epoch,
      data["bindingEpoch"].as<int>(), state == "superseded", revocation_error);
}

} // namespace lunaredu::lunaros::platform_link
