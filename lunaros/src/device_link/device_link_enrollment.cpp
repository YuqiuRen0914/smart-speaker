#include "device_link/device_link_operations.h"

#include <ArduinoJson.h>

#include "lunaredu_lunaros/persistence/identity.h"
#include "lunaredu_lunaros/kernel/log.h"
#include "lunaredu_lunaros/persistence/settings.h"
#include "device_link/device_link_http.h"
#include "device_link/device_link_state.h"

namespace lunaredu::lunaros::device_link {
namespace {

constexpr const char *kTag = "device_link";

}  // namespace

bool enroll_open_claim() {
  const DeviceLinkConfig &runtime = runtime_config();
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
  if (!perform_public_request("POST", "/api/hardware/device-enrollment/open",
                              &body, &response, &status_code)) {
    LOGW(kTag, "open claim enrollment failed: status=%d", status_code);
    return false;
  }
  if (!(response["data"]["accepted"] | false)) {
    LOGW(kTag, "open claim enrollment returned no acceptance");
    return false;
  }
  return true;
}

}  // namespace lunaredu::lunaros::device_link
