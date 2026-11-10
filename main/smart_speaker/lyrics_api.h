#pragma once
#include "device_api/device_operation_types.h"
namespace smart_speaker {
lunaredu::lunaros::DeviceOperationResult handle_lyrics_upload(
    lunaredu::lunaros::DeviceApiContext &,
    const lunaredu::lunaros::DeviceBulkRequest &request, JsonObject response);
lunaredu::lunaros::DeviceOperationResult handle_lyrics_remove(
    lunaredu::lunaros::DeviceApiContext &, JsonVariantConst request,
    JsonObject response);
lunaredu::lunaros::DeviceOperationResult handle_lyrics_offset(
    lunaredu::lunaros::DeviceApiContext &, JsonVariantConst request,
    JsonObject response);
}
