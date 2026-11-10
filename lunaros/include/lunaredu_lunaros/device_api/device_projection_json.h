#pragma once

#include <string>

#include <ArduinoJson.h>

#include "lunaredu_lunaros/device_api/device_projection.h"

namespace lunaredu::lunaros {

DeviceOperationResult device_operation_error(
    const char *error, uint16_t status_code,
    const char *fallback_error = "device_projection_request_failed");

DeviceOperationResult device_operation_ack(uint16_t status_code = 200);

std::string device_operation_string(JsonVariantConst value);

} // namespace lunaredu::lunaros
