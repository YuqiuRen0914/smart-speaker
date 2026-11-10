#pragma once

#include <ArduinoJson.h>

namespace lunaredu::lunaros {

// Device API-only JSON projection of the typed Settings snapshot. Passwords
// are never copied into the response.
struct DeviceWifiConfig;
// Both existing response shapes share redaction and credential traversal.
void fill_saved_wifi_config_payload(JsonObject payload,
                                    const DeviceWifiConfig &wifi,
                                    bool provisioning_response = false);

} // namespace lunaredu::lunaros
