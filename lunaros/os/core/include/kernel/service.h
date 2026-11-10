#pragma once

#include <vector>
#include <array>
#include <stddef.h>
#include <stdint.h>
#include <string>
#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif

namespace lunaredu::lunaros {

// Kernel readiness barrier implemented by Runtime bootstrap. Services depend
// on this contract without importing or depending on the Runtime component.
inline constexpr const char *kKernelReadyServiceId = "system.runtime";

enum class ServiceCriticality {
  Required,
  Optional,
};

constexpr const char *service_criticality_name(ServiceCriticality value) {
  switch (value) {
  case ServiceCriticality::Required:
    return "required";
  case ServiceCriticality::Optional:
    return "optional";
  }
  return "optional";
}

enum class ServiceState {
  Registered,
  Starting,
  Ready,
  Degraded,
  Stopping,
  Stopped,
  Failed,
};

constexpr const char *service_state_name(ServiceState state) {
  switch (state) {
  case ServiceState::Registered:
    return "registered";
  case ServiceState::Starting:
    return "starting";
  case ServiceState::Ready:
    return "ready";
  case ServiceState::Degraded:
    return "degraded";
  case ServiceState::Stopping:
    return "stopping";
  case ServiceState::Stopped:
    return "stopped";
  case ServiceState::Failed:
    return "failed";
  }
  return "failed";
}

struct ServiceDependency {
  const char *id = nullptr;
  bool required = true;
};

struct ServiceDefinition {
  const char *id = nullptr;
  const char *title = nullptr;
  const char *owner_executor = nullptr;
  ServiceCriticality criticality = ServiceCriticality::Optional;
  const ServiceDependency *dependencies = nullptr;
  size_t dependency_count = 0;
  // Execution resources, independent of dependency/failure criticality.
  // Names and this array must outlive the installed product.
  const char *const *execution_resources = nullptr;
  size_t execution_resource_count = 0;
};

// Background execution is shared during normal operation. A configuration or
// maintenance operation can reserve it exclusively before allocating its peak
// resources. Consumers declare this requirement; the OS never lists products.
inline constexpr const char *kBackgroundExecutionResource = "execution.background";
struct ServiceResourceWindow { uint64_t generation = 0; };
// A use survives temporary unavailability, but never authorizes bypassing a
// provider's admission policy. Release it when the requesting activity ends.
struct ServiceUse { uint64_t generation = 0; };

struct ServiceSnapshot {
  // Registrations are immutable after startup. Keeping immutable metadata as
  // stable references makes a complete registry snapshot safe for small
  // FreeRTOS/HTTP task stacks.
  const char *id = nullptr;
  const char *title = nullptr;
  const char *owner_executor = nullptr;
  ServiceCriticality criticality = ServiceCriticality::Optional;
  ServiceState state = ServiceState::Registered;
  bool available = false; // Own state plus all required dependencies.
  std::string detail;
  uint32_t revision = 0;
  size_t explicit_uses = 0;
  bool on_demand = false, has_admission_policy = false;
  size_t dependency_count = 0;
  struct Dependency {
    const char *id = nullptr;
    bool required = true;
    bool available = false;
  };
  std::vector<Dependency> dependencies;
};

struct ServiceRegistryStorageSnapshot {
  const char *memory = "none";
  size_t capacity = 0;
  // Actual registered record payload; excludes allocator overhead, separately owned
  // string buffers, the control object and its synchronization storage.
  size_t storage_bytes = 0;
};

struct ServiceRegistrySnapshot {
  uint32_t revision = 0;
  bool required_services_ready = false;
  size_t service_count = 0;
  std::vector<ServiceSnapshot> services;
  ServiceRegistryStorageSnapshot storage;
};

// Product and sibling components consume only immutable facts. Registry
// storage, locks and transition methods are private Kernel implementation.
// Effective availability includes all transitive required dependencies. The
// snapshot's state remains the service's own reported lifecycle state.

enum class ServiceStartDisposition {
  Ready,
  Degraded,
  Deferred,
  Failed,
};

struct ServiceStartResult {
  ServiceStartDisposition disposition = ServiceStartDisposition::Failed;
  std::string detail;
};

using ServiceStartCallback = ServiceStartResult (*)(void *context);
using ServiceStopCallback = bool (*)(void *context, std::string &error);
enum class ServiceStopDisposition { Stopped, Pending, Failed };
struct ServiceStopResult {
  ServiceStopDisposition disposition = ServiceStopDisposition::Failed;
  std::string detail;
};
using ServiceStopStep = ServiceStopResult (*)(void *context);
using ServiceDemandCallback = bool (*)(void *context);
struct ServicePolicyKey {
  std::array<uint64_t,2> values{};
  bool operator==(const ServicePolicyKey &v) const{return values==v.values;}
};
// Owner-provided exact keys cover every predicate input, including deadlines.
// No key means live observation as before. Storage is borrowed for the complete
// composition lifetime; the owner chooses its memory region.
struct ServicePolicyMemo {
  ServicePolicyKey key;
  const void *owner=nullptr;
  uint32_t registry_revision=0,demand_revision=0;
  bool valid=false,admitted=false,demanded=false;
};

// A ServiceNode is the complete, explicit lifecycle contribution installed by
// a product definition. Nothing is auto-registered through global
// constructors or string-topic side effects.
struct ServiceNode {
  ServiceDefinition definition;
  ServiceStartCallback start = nullptr;
  ServiceStopCallback stop = nullptr;
  void *context = nullptr;
  // Deferred services use activate on their declared owner executor. This
  // keeps asynchronous startup inside ServiceHost instead of letting product
  // tasks call concrete service implementations directly.
  ServiceStartCallback activate = nullptr;
  // Read-only intent, evaluated on the lifecycle owner. A null callback means
  // boot demand. Optional execution may be retired while declarations remain.
  ServiceDemandCallback demanded = nullptr;
  // A bounded stop step keeps Stopping and resources until actual retirement.
  ServiceStopStep stop_step = nullptr;
  // Absolute admission (identity, enabled preference, hardware, etc.). Unlike
  // baseline demand, neither an explicit use nor a dependent can override it.
  ServiceDemandCallback admitted = nullptr;
  // No boot demand. Required consumers and explicit uses wake this provider.
  bool on_demand = false;
  // Side-effect-free check on the service owner, before entering Stopping.
  // Rejection preserves availability and all dependencies.
  bool (*before_stop)(void *, std::string &) = nullptr;
  ServicePolicyKey (*policy_key)(void *)=nullptr;
  ServicePolicyMemo *policy_memo=nullptr;
};

} // namespace lunaredu::lunaros
