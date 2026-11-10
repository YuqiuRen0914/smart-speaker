#pragma once
#include "model/device_model.h"
namespace lunaredu::lunaros {
class KernelRuntime;
// Core execution and neutral device model; higher owners compose this context.
struct ExecutionContext {
  explicit ExecutionContext(KernelRuntime &execution) : runtime(execution) {}
  ExecutionContext(const ExecutionContext &) = delete;
  ExecutionContext &operator=(const ExecutionContext &) = delete;
  KernelRuntime &runtime;
  DeviceRuntimeContext model;
};
}
