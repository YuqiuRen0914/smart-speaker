#include "device_link/device_link_operations.h"

#include <algorithm>
#include <ArduinoJson.h>

#include "lunaredu_lunaros/kernel/log.h"
#include "lunaredu_lunaros/persistence/settings.h"
#include "lunaredu_lunaros/persistence/identity.h"
#include "device_link/device_link_http.h"
#include "kernel/system_primitives.h"
#include "device_link/device_link_state.h"
#include "lunaredu_lunaros/delivery/resources.h"

namespace lunaredu::lunaros::device_link {
namespace {

constexpr const char *kTag = "device_link";

std::string create_pairing_code() {
  constexpr uint32_t kPairingCodeRange = 1000000;
  const uint32_t value =
      internal::system_random_u32() % kPairingCodeRange;
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
    const uint32_t value = internal::system_random_u32();
    for (size_t byte = 0; byte < 4; ++byte) {
      const uint8_t part = static_cast<uint8_t>(value >> (byte * 8));
      nonce[index + byte * 2] = kHex[part >> 4];
      nonce[index + byte * 2 + 1] = kHex[part & 0x0f];
    }
  }
  return nonce;
}

bool create_pairing_session() {
  std::string clock_error;
  if (!ensure_network_clock(clock_error)) {
    update_pairing_snapshot(DeviceLinkBindingState::Error, {}, {}, 0,
                            clock_error.empty() ? "time_sync_failed"
                                                : clock_error);
    return false;
  }
  if (!account_identity_ready()) {
    update_pairing_snapshot(DeviceLinkBindingState::Error, {}, {}, 0,
                            "identity_not_ready");
    return false;
  }
  const std::string nonce = create_pairing_nonce();
  const DeviceConfig &config = device_config();
  const DeviceLinkConfig &runtime = runtime_config();
  const std::string pairing_code = runtime.fixed_pairing_code.empty()
                                       ? create_pairing_code()
                                       : runtime.fixed_pairing_code;

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
          "POST", "/api/hardware/device-enrollment/pairing-sessions", &body,
          &response, &status_code)) {
    update_pairing_snapshot(DeviceLinkBindingState::Error, {}, {}, 0,
                            pairing_failure_code(response, status_code));
    return false;
  }
  const std::string code = response["data"]["pairing"]["code"] | "";
  const std::string bind_path =
      response["data"]["pairing"]["bindPath"] | "";
  const int64_t expires_at_ms =
      response["data"]["pairing"]["expiresAtMs"] | 0LL;
  if (code.empty() || bind_path.empty() || expires_at_ms <= 0) {
    update_pairing_snapshot(DeviceLinkBindingState::Error, {}, {}, 0,
                            "pairing_response_invalid");
    return false;
  }
  update_pairing_snapshot(DeviceLinkBindingState::Pairing, code,
                          std::string(platform_base_url()) + bind_path,
                          expires_at_ms);
  LOGI(kTag, "pairing session ready: code=%s", code.c_str());
  return true;
}

bool poll_pairing_session() {
  JsonDocument response;
  if (!perform_request("GET",
                       "/api/hardware/device-runtime/pairing-sessions",
                       nullptr, &response)) {
    return false;
  }
  const std::string status = response["data"]["deviceStatus"] | "";
  const int binding_epoch = response["data"]["bindingEpoch"] | 0;
  if (status == "active") {
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
    return true;
  }
  if (response["data"]["pairing"].isNull()) {
    update_pairing_snapshot(DeviceLinkBindingState::Unbound, {}, {}, 0,
                            {}, binding_epoch);
  }
  return true;
}

bool cancel_pairing_session() {
  if (!perform_request("DELETE",
                       "/api/hardware/device-runtime/pairing-sessions",
                       nullptr, nullptr)) {
    return false;
  }
  update_pairing_snapshot(DeviceLinkBindingState::Unbound);
  return true;
}

bool unbind_device() {
  const DeviceLinkPairingSnapshot before = pairing_snapshot();
  if (before.state != DeviceLinkBindingState::Bound) {
    update_pairing_snapshot(DeviceLinkBindingState::Error, {}, {}, 0,
                            "device_not_bound", before.binding_epoch);
    return false;
  }
  JsonDocument response;
  int status_code = 0;
  if (!perform_request("DELETE", "/api/hardware/device-runtime/binding",
                       nullptr, &response, &status_code)) {
    const std::string server_code = response["code"] | "";
    // The upper computer may have released the account first.  The server's
    // 409/device_not_bound is then an idempotent acknowledgement, not a
    // transport failure.  Reconcile the local projection immediately; the
    // next presence still supplies the authoritative epoch and account data.
    if (status_code == 409 && server_code == "device_not_bound") {
      const int next_epoch = before.binding_epoch + 1;
      set_private_resource_scope(static_cast<uint32_t>(std::max(0, next_epoch)),
                                 "");
      update_pairing_snapshot(DeviceLinkBindingState::Unbound, {}, {}, 0, {},
                              next_epoch);
      LOGI(kTag, "device binding already released by platform: epoch=%d",
           next_epoch);
      return true;
    }
    update_pairing_snapshot(DeviceLinkBindingState::Error, {}, {}, 0,
                            "unbind_request_failed", before.binding_epoch);
    return false;
  }
  const int next_epoch = response["data"]["bindingEpoch"] | (before.binding_epoch + 1);
  set_private_resource_scope(static_cast<uint32_t>(std::max(0, next_epoch)),
                             "");
  update_pairing_snapshot(DeviceLinkBindingState::Unbound, {}, {}, 0, {},
                          next_epoch);
  LOGI(kTag, "device binding released locally: epoch=%d", next_epoch);
  return true;
}

} // namespace lunaredu::lunaros::device_link
