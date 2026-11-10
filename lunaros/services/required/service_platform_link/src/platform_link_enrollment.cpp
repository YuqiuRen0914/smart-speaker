#include "platform_link_operations.h"

#include <ArduinoJson.h>

#include "system/identity.h"
#include <esp_log.h>
#include "configuration/device_config.h"
#include "configuration/settings.h"
#include "platform/system.h"
#include "platform_link_http.h"
#include "platform_link_state.h"

namespace lunaredu::lunaros::platform_link {
namespace {

constexpr const char *kTag = "platform_link";

struct ProfileSync {
  std::string profile;
  bool acknowledged = false, durable = false, blocked = false, waiting = false;
  uint32_t retry_at = 0;
};
// Only the existing Network worker calls enrollment, discovery and presence.
ProfileSync profile_sync;
constexpr uint32_t kProfileRetryMs = 30000;

std::string product_profile() {
  const auto &runtime = runtime_config();
  JsonDocument record;
  record["schema"] = 1;
  // Existing cache scope contains origin + credential digest, never credential.
  record["scope"] = binding_profile_tag();
  record["boardType"] = runtime.board_type.empty() ? "unknown" : runtime.board_type;
  record["model"] = runtime.model.empty() ? "LunarEdu ESP32 Device" : runtime.model;
  record["firmwareVersion"] = firmware_version();
  std::string serialized;
  serializeJson(record, serialized);
  return serialized;
}
ProfileSync &current_profile() {
  const auto profile = product_profile();
  if (profile_sync.profile != profile) {
    profile_sync = {};
    profile_sync.profile = profile;
  }
  return profile_sync;
}
void defer_profile(ProfileSync &state) {
  state.waiting = true;
  state.retry_at = monotonic_ms() + kProfileRetryMs;
}
bool persist_profile(ProfileSync &state) {
  state.durable = settings_set_string("lunar_link", "profile", state.profile);
  if (!state.durable) defer_profile(state);
  return state.durable;
}

bool submit_product_profile(int *http_status, bool report_binding_error);

}  // namespace

bool enroll_open_claim(int *http_status) {
  return submit_product_profile(http_status, true);
}

namespace {
bool submit_product_profile(int *http_status, bool report_binding_error) {
  if(http_status)*http_status=-1;
  const PlatformLinkConfig &runtime = runtime_config();
  if (!runtime.open_claim || !account_identity_ready()) {
    return false;
  }

  const DeviceConfig &config = device_config();
  JsonDocument request;
  request["serialNumber"] = hardware_serial_number();
  request["credential"] = config.cloud.credential;
  request["displayName"] = config.identity.name;
  request["boardType"] = runtime.board_type.empty() ? "unknown" : runtime.board_type;
  request["model"] = runtime.model.empty() ? "LunarEdu ESP32 Device" : runtime.model;
  request["firmwareVersion"] = firmware_version();
  add_capabilities(request["capabilities"].to<JsonArray>());

  std::string body;
  serializeJson(request, body);
  JsonDocument response;
  int status_code = 0;
  if (!perform_public_request("POST", "/api/link/device-enrollment/open",
                              &body, &response, &status_code)) {
    if(http_status)*http_status=status_code;
    const std::string server_code=response["code"] | "";
    if(report_binding_error && (server_code=="device_already_bound" || server_code=="device_retired"))
      update_pairing_snapshot(PlatformLinkBindingState::Error,{}, {},0,server_code);
    ESP_LOGW(kTag, "open claim enrollment failed: status=%d", status_code);
    return false;
  }
  if(http_status)*http_status=status_code;
  if (!(response["data"]["accepted"] | false)) {
    update_platform_request_diagnostics("enrollment","platform_response_invalid");
    update_platform_connection(PlatformLinkPlatformConnectionState::Error,status_code,"platform_response_invalid");
    ESP_LOGW(kTag, "open claim enrollment returned no acceptance");
    return false;
  }
  record_product_profile_success();
  return true;
}
} // namespace

void record_product_profile_success() {
  auto &state = current_profile();
  if (state.acknowledged && (state.durable ||
      (state.waiting && static_cast<int32_t>(monotonic_ms() - state.retry_at) < 0))) return;
  // Remember successful HTTP before committing the hint. A flash failure must
  // retry only the commit, not repeat a successful enrollment on every presence.
  state.acknowledged = true;
  state.blocked = false;
  state.waiting = false;
  (void)persist_profile(state);
}

bool reconcile_product_profile_after_presence() {
  if (!runtime_config().open_claim) return true;
  auto &state = current_profile();
  if (state.waiting && static_cast<int32_t>(monotonic_ms() - state.retry_at) < 0)
    return state.acknowledged;
  state.waiting = false;
  if (state.acknowledged) {
    if (!state.durable) (void)persist_profile(state);
    return true;
  }
  if (state.blocked) return false;
  std::string saved, error;
  const auto read = settings_read_string("lunar_link", "profile", 2048, saved, error);
  if (read == SettingsReadResult::Failed) { defer_profile(state); return false; }
  if (read == SettingsReadResult::Found && saved == state.profile) {
    state.acknowledged = state.durable = true;
    return true;
  }
  // Caller has just authenticated this exact credential and committed the
  // server binding. The legacy endpoint preserves active ownership for the
  // same credential; this path never generates/replaces a credential.
  int status = -1;
  const bool updated = submit_product_profile(&status, false);
  if (!updated) {
    if (status == 400 || status == 401 || status == 403 || status == 409)
      state.blocked = true;
    else defer_profile(state);
  }
  // Even after success, this response's catalog was selected before updating
  // the product identity. Read the new catalog on the next presence.
  return false;
}

bool product_profile_current() {
  if (!runtime_config().open_claim) return true;
  auto &state = current_profile();
  if (state.acknowledged) return true;
  std::string saved, error;
  return settings_read_string("lunar_link", "profile", 2048, saved, error) ==
      SettingsReadResult::Found && saved == state.profile;
}

}  // namespace lunaredu::lunaros::platform_link
