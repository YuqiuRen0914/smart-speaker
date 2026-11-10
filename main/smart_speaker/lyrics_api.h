#pragma once
#include "lunaredu_lunaros/device_api/device_projection.h"
namespace smart_speaker {
using namespace lunaredu::lunaros;
DeviceOperationResult handle_lyrics_upload(const DeviceBulkRequest &request, JsonObject response);
DeviceOperationResult handle_lyrics_remove(JsonVariantConst request, JsonObject response);
DeviceOperationResult handle_lyrics_offset(JsonVariantConst request, JsonObject response);
}
