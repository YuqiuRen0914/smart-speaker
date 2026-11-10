#pragma once
#include "device_api/device_operation_types.h"

namespace lunaredu::lunaros {
// Borrowed only for this call. Foreign-owner dispatch copies names and JSON,
// waits up to five seconds, and can return completion_unknown for a write
// already running. Query actual state; never automatically replay that write.
// Same-owner/Caller execution retains the direct synchronous path.
// Declarative documents supply names,
// never a diagnostic token or an authority grant. Owners explicitly opt in via
// composable or the existing remote Query/Command declaration.
struct ComposedDeviceOperation {
  const char *module;
  const char *operation;
  const char *capability;
  bool command;
  // Trusted invoking owner, supplied by code rather than the document.
  const char *caller_module;
};
DeviceOperationResult check_composed_device_operation(
    DeviceApiContext &, const ComposedDeviceOperation &,
    JsonVariantConst input = JsonVariantConst{});
DeviceOperationResult invoke_composed_device_operation(
    DeviceApiContext &, const ComposedDeviceOperation &, JsonVariantConst,
    JsonObject);
}
