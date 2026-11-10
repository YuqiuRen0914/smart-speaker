#pragma once

#include <ArduinoJson.h>

namespace lunaredu::lunaros {
struct DeviceApiContext;

struct LocalDeviceIdentityConfig;
void fill_device_identity_payload([[maybe_unused]] DeviceApiContext &device, JsonObject payload);
void fill_device_identity_payload(JsonObject payload,
                                   const LocalDeviceIdentityConfig &identity);

} // namespace lunaredu::lunaros
