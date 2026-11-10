#pragma once
#include "device_api/device_operation.h"
#include "device_api/device_api_context.h"
#include "device_api/operation_dispatch.h"
#include <cstdio>

namespace lunaredu::lunaros {
DeviceOperationCorrelation begin_device_operation_feedback(DeviceApiContext &device);

void fill_device_operation_correlation(JsonObject response,
                                              const DeviceOperationCorrelation &correlation);

// Explicit context survives executor handoff; no thread-local/global current request.
void finish_device_operation_feedback(DeviceApiContext &device, const DeviceModuleSpec &module,
    const DeviceOperationSpec &operation, const DeviceOperationResult &result,
    JsonObject data, JsonObject response, const DeviceOperationCorrelation &correlation);
}
