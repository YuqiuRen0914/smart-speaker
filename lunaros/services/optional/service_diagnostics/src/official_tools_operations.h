#pragma once
#include "device_api/device_operation_builders.h"
namespace lunaredu::lunaros::internal {
void fill_official_tools_status(JsonObject);
DeviceOperationResult start_official_tools(JsonVariantConst, JsonObject);
DeviceOperationResult stop_official_tools(JsonVariantConst, JsonObject);
}
