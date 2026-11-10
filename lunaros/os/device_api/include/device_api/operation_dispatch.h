#pragma once

#include "model/device_model.h"
#include "kernel/runtime.h"

namespace lunaredu::lunaros {

bool dispatch_device_operation(KernelRuntime &runtime,
                                      const DeviceOperationSpec &model,
                                      ServiceOperation operation, void *context,
                                      std::string &error, const char *module = nullptr,
                                      DeviceOperationCorrelation *correlation = nullptr);

} // namespace lunaredu::lunaros
