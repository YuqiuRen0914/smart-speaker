#include "device_api/projections/wifi_config_payload.h"

#include "lunaredu_lunaros/persistence/settings.h"

namespace lunaredu::lunaros {

void fill_saved_wifi_config_payload(JsonObject payload,
                                    const DeviceWifiConfig &wifi,
                                    bool provisioning_response) {
  payload["configured"] = wifi.configured;
  payload["last_success_index"] = wifi.last_success_index;
  payload["max_credentials"] = kDeviceWifiCredentialMax;
  JsonArray credentials = payload["credentials"].to<JsonArray>();
  for (uint8_t index = 0; index < wifi.credential_count; ++index) {
    JsonObject item = credentials.add<JsonObject>();
    item["ssid"] = wifi.credentials[index].ssid;
    const bool has_password = !wifi.credentials[index].password.empty();
    item["password"] = "";
    item["password_set"] = has_password;
    item["password_masked"] = provisioning_response || has_password;
    if (provisioning_response) item["password_visible"] = false;
  }
}

} // namespace lunaredu::lunaros
