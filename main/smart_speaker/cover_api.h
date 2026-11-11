#pragma once
#include "smart_speaker_runtime.h"

namespace smart_speaker {
DeviceOperationResult handle_cover_upload(const DeviceBulkRequest &, JsonObject);
DeviceOperationResult handle_cover_remove(JsonVariantConst, JsonObject);
DeviceOperationResult handle_cover_image(const DeviceMediaRequest &, const DeviceMediaWriter &);
}
