#pragma once
#include "device_api/device_operation.h"
namespace lunaredu::lunaros::internal {
void fill_espnow_management(DeviceApiContext &,JsonObject);
DeviceOperationResult open_espnow_management(DeviceApiContext &,JsonVariantConst,JsonObject);
DeviceOperationResult close_espnow_management(DeviceApiContext &,JsonVariantConst,JsonObject);
}
