#pragma once
#include "kernel/execution_context.h"
#include "device_api/runtime_observation.h"
namespace lunaredu::lunaros {
class DeviceProjectionRegistry;
class DeviceHostConnectionTracker;
class DeviceApiBinding;
class DeviceApiObservationBinding;
struct DeviceApiRuntimeObservation {
  void *context = nullptr;
  BootHealthObservation (*health)(void *) = nullptr;
  BootstrapMetrics (*metrics)(void *) = nullptr;
};
// Protocol bindings and borrowed observations. Mutable boot state and the
// application controller remain with their respective owners.
struct DeviceApiContext : virtual ExecutionContext {
  explicit DeviceApiContext(KernelRuntime &execution) : ExecutionContext(execution) {}
  DeviceApiContext(const DeviceApiContext &) = delete;
  DeviceApiContext &operator=(const DeviceApiContext &) = delete;
  DeviceProjectionRegistry *projections() const { return projections_; }
  const void *binding(const void *key) const;
  BootHealthObservation boot_health_observation() const {
    return runtime_observation_.health ? runtime_observation_.health(runtime_observation_.context) : BootHealthObservation{};
  }
  BootstrapMetrics bootstrap_metrics() const {
    return runtime_observation_.metrics ? runtime_observation_.metrics(runtime_observation_.context) : BootstrapMetrics{};
  }
private:
  friend class DeviceProjectionRegistry;
  friend class DeviceHostConnectionTracker;
  friend class DeviceApiBinding;
  friend class DeviceApiObservationBinding;
  DeviceApiBinding *bindings_ = nullptr;
  DeviceApiRuntimeObservation runtime_observation_{};
  DeviceProjectionRegistry *projections_ = nullptr;
  DeviceHostConnectionTracker *host_connection_ = nullptr;
};

// Adapter-owned, allocation-free binding. Keys have static storage; values and
// bindings outlive every callback. Composition serializes changes and stops
// readers before withdrawal. The framework knows no domain context type.
class DeviceApiBinding {
public:
  DeviceApiBinding(DeviceApiContext &api, const void *key, const void *value)
      : api_(api), key_(key), value_(value) {
    if (key && value && !api.binding(key)) {
      next_ = api.bindings_; api.bindings_ = this; bound_ = true;
    }
  }
  ~DeviceApiBinding() {
    if (!bound_) return;
    auto **slot = &api_.bindings_;
    while (*slot && *slot != this) slot = &(*slot)->next_;
    if (*slot == this) *slot = next_;
  }
  DeviceApiBinding(const DeviceApiBinding &) = delete;
  DeviceApiBinding &operator=(const DeviceApiBinding &) = delete;
  bool owns_context() const { return bound_; }
private:
  friend struct DeviceApiContext;
  DeviceApiContext &api_;
  const void *key_, *value_;
  DeviceApiBinding *next_ = nullptr;
  bool bound_ = false;
};
inline const void *DeviceApiContext::binding(const void *key) const {
  for (auto *entry = bindings_; entry; entry = entry->next_)
    if (entry->key_ == key) return entry->value_;
  return nullptr;
}

class DeviceApiObservationBinding {
public:
  DeviceApiObservationBinding(DeviceApiContext &api, DeviceApiRuntimeObservation observation)
      : api_(api), observation_(observation) {
    if (observation.context && !api.runtime_observation_.context) {
      api.runtime_observation_ = observation; bound_ = true;
    }
  }
  ~DeviceApiObservationBinding() {
    if (bound_ && api_.runtime_observation_.context == observation_.context)
      api_.runtime_observation_ = {};
  }
  DeviceApiObservationBinding(const DeviceApiObservationBinding &) = delete;
  DeviceApiObservationBinding &operator=(const DeviceApiObservationBinding &) = delete;
  bool owns_context() const { return bound_; }
private:
  DeviceApiContext &api_;
  DeviceApiRuntimeObservation observation_;
  bool bound_ = false;
};
}
