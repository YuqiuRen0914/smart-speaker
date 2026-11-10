#include "device_api/routes/device_operation_dispatch.h"
#include <cassert>
#include <cstring>

namespace {
int dispatches = 0;
int executions = 0;
void callback(void *ctx, lunaredu::lunaros::ServiceOperationResult &result) {
  ++executions;
  result.ok = ctx != nullptr;
  if (!result.ok) result.error = "callback_failed";
}
}

namespace lunaredu::lunaros {
bool invoke_service_operation(const char *owner, ServiceOperation operation,
                              void *context, std::string &error, uint32_t) {
  assert(std::strcmp(owner, kMainServiceExecutor) == 0);
  ++dispatches;
  ServiceOperationResult result;
  operation(context, result);
  error = result.error;
  return result.ok;
}
}

int main() {
  using namespace lunaredu::lunaros;
  DeviceOperationSpec model;
  model.owner_executor = kMainServiceExecutor;
  std::string error;
  int context = 1;
  assert(internal::dispatch_device_operation(model, callback, &context, error));
  assert(dispatches == 1 && executions == 1);
  model.execution = DeviceOperationExecution::Caller;
  // A contradictory owner declaration must never silently change dispatch.
  assert(!internal::dispatch_device_operation(model, callback, &context, error));
  model.owner_executor = nullptr;
  assert(internal::dispatch_device_operation(model, callback, &context, error));
  assert(dispatches == 1 && executions == 2);
  assert(!internal::dispatch_device_operation(model, callback, nullptr, error));
  assert(error == "callback_failed");
  model.execution = static_cast<DeviceOperationExecution>(255);
  assert(!internal::dispatch_device_operation(model, callback, &context, error));
  assert(executions == 3);
}
