#pragma once

#include "device_api/device_operation_types.h"

namespace lunaredu::lunaros::internal {

void fill_diagnostic_session_status(DeviceApiContext &device, JsonObject payload);
DeviceOperationResult open_system_diagnostic_session(DeviceApiContext &device,
                                                      JsonVariantConst request,
                                                      JsonObject response);
DeviceOperationResult close_system_diagnostic_session(DeviceApiContext &device,
                                                       JsonVariantConst request,
                                                       JsonObject response);

} // namespace lunaredu::lunaros::internal
