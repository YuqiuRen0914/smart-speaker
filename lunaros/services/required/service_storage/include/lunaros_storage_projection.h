#pragma once
#include "device_api/device_projection.h"
namespace lunaredu::lunaros {
const DeviceProjectionContribution &storage_device_projection();
// Immutable declaration range, borrowed for the firmware lifetime.
DeviceOperationsView storage_device_operations();
}
