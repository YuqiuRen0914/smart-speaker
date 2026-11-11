#pragma once

#include "lunaredu_lunaros/device_api/device_projection.h"

namespace smart_speaker {
using lunaredu::lunaros::DeviceBulkRequest;
using lunaredu::lunaros::DeviceOperationResult;
DeviceOperationResult handle_pet_frame_upload(const DeviceBulkRequest &, JsonObject);
}
