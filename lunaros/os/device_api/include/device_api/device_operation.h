#pragma once
#include "device_api/device_operation_types.h"
#include "model/device_model.h"

namespace lunaredu::lunaros {
// A contributor declares the model and one transport projection together so
// the operation ID lives only in the model. Consumers receive the two
// views independently after sealing.
struct DeviceOperationDefinition {
  lunaredu::lunaros::DeviceOperationSpec model;
  DeviceOperationProjection projection;
};
struct DeviceOperationsView { const DeviceOperationDefinition *data=nullptr; size_t count=0; };

} // namespace lunaredu::lunaros
