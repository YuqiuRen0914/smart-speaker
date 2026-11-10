#pragma once

#include <ArduinoJson.h>

namespace lunaredu::lunaros {
struct DeviceApiContext;
namespace internal {
void fill_system_diagnostics_payload(DeviceApiContext &device, JsonObject payload);
} // namespace internal
} // namespace lunaredu::lunaros
