#pragma once

#include "lunaredu_lunaros/model/device_model.h"
#include "lunaredu_lunaros/kernel/service_executor.h"

namespace lunaredu::lunaros::internal {

inline bool dispatch_device_operation(const DeviceOperationSpec &model,
                                      ServiceOperation operation, void *context,
                                      std::string &error) {
  error.clear();
  if (!device_operation_execution_valid(model) || operation == nullptr) {
    error = "invalid_device_operation_execution";
    return false;
  }
  if (model.execution == DeviceOperationExecution::Caller) {
    ServiceOperationResult status;
    operation(context, status);
    error = status.error;
    return status.ok;
  }
  return invoke_service_operation(model.owner_executor, operation, context, error);
}

} // namespace lunaredu::lunaros::internal
