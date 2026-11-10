#pragma once

#include <array>
#include <stddef.h>
#include <stdint.h>
#include <string>

namespace lunaredu::lunaros {

constexpr size_t kServiceCapacity = 16;
constexpr size_t kServiceDependencyCapacity = 6;

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
};

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
  size_t dependency_count = 0;
  struct Dependency {
    const char *id = nullptr;
    bool required = true;
    bool available = false;
  };
  std::array<Dependency, kServiceDependencyCapacity> dependencies;
};

struct ServiceRegistrySnapshot {
  uint32_t revision = 0;
  bool required_services_ready = false;
  size_t service_count = 0;
  std::array<ServiceSnapshot, kServiceCapacity> services;
};

// Product and sibling components consume only immutable facts. Registry
// storage, locks and transition methods are private Kernel implementation.
// Effective availability includes all transitive required dependencies. The
// snapshot's state remains the service's own reported lifecycle state.
bool service_available(const char *service_id);
bool required_services_ready(std::string *reason = nullptr);
ServiceRegistrySnapshot service_registry_snapshot();

} // namespace lunaredu::lunaros
