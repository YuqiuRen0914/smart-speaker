#pragma once
#include "device_api/device_projection.h"
namespace lunaredu::lunaros {
const DeviceProjectionContribution &system_base_projection();
// Immutable declaration range, borrowed for the firmware lifetime.
DeviceOperationsView system_base_operations();
void fill_system_base_capabilities(DeviceApiContext &, JsonObject);
}
