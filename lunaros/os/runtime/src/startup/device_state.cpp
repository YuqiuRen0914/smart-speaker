#include "runtime/device_state.h"
#include "maintenance/update_maintenance.h"
#include "device_api/adapters/application_projection.h"
#include <cstdlib>

namespace lunaredu::lunaros {
RuntimeDeviceState::RuntimeDeviceState(DeviceContext &device) : device_(device), maintenance_binding_(device,maintenance_), application_ports_(device,device), frame_owner_(device.runtime,false), observation_(device, {this,
    [](void *opaque) { return BootHealthObservation{static_cast<RuntimeDeviceState *>(opaque)->boot_health_observation_.load(std::memory_order_acquire)}; },
    [](void *opaque) {
      auto &state = *static_cast<RuntimeDeviceState *>(opaque);
      BootstrapMetrics value;
      value.available = state.bootstrap_available_.load(std::memory_order_acquire);
      value.completed = state.bootstrap_completed_.load(std::memory_order_acquire);
      value.succeeded = state.bootstrap_succeeded_.load(std::memory_order_relaxed);
      value.total_ms = state.bootstrap_total_ms_.load(std::memory_order_relaxed);
      for (size_t i = 0; i < value.stage_ms.size(); ++i) value.stage_ms[i] = state.bootstrap_stage_ms_[i].load(std::memory_order_relaxed);
      return value;
    }}) {
  if (device_.runtime_state_) std::abort();
  device_.runtime_state_ = this;
  if (!maintenance_binding_.owns_context() || !application_ports_.owns_context() || !observation_.owns_context()) std::abort();
}
RuntimeDeviceState::~RuntimeDeviceState() {
  device_.runtime_state_ = nullptr;
}
}
