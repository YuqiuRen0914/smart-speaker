#pragma once
#include "app/execution/application_context.h"
#include "device_api/device_api_context.h"
namespace lunaredu::lunaros {
class RuntimeDeviceState;
// One Core execution/model base, composed from narrower owner views.
// Application and Device API never include this Runtime aggregate.
struct DeviceContext final : ApplicationContext, DeviceApiContext {
  explicit DeviceContext(KernelRuntime &execution)
      : ExecutionContext(execution), ApplicationContext(execution), DeviceApiContext(execution) {}
  DeviceContext(const DeviceContext &) = delete;
  DeviceContext &operator=(const DeviceContext &) = delete;
private:
  friend class ProductRuntimeAccess;
  friend class RuntimeDeviceState;
  RuntimeDeviceState *runtime_state_ = nullptr;
};
}
