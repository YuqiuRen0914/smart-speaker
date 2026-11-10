#pragma once

#include <ArduinoJson.h>
#include <string>
#include "lunaredu_lunaros/device_api/standard_projections.h"

namespace lunaredu::lunaros {

struct LocalDeviceIdentityConfig;
void fill_device_identity_payload(JsonObject payload);
void fill_device_identity_payload(JsonObject payload,
                                   const LocalDeviceIdentityConfig &identity);
void fill_device_bootstrap_payload(JsonObject payload);
void fill_device_status_payload(JsonObject payload);

} // namespace lunaredu::lunaros
