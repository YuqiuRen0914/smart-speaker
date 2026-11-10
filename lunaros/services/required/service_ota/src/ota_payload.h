#pragma once
#include "device_api/device_api_context.h"

#include <ArduinoJson.h>

namespace lunaredu::lunaros {

void fill_ota_status_payload([[maybe_unused]] DeviceApiContext &device, JsonObject payload);

} // namespace lunaredu::lunaros
