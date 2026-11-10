#pragma once
#include <cstddef>
#include <string>
#include "kernel/product_definition.h"
#include "device_api/projection_contribution.h"
#include "runtime/execution_contribution.h"
#include <span>
namespace lunaredu::lunaros {
struct DeviceContext; class DeviceProjectionRegistry;
// Boot-lived, bounded observations evaluated on the existing Main owner.
struct RuntimeHealthProbe {const char *id=nullptr;void *context=nullptr;bool (*ready)(void *)=nullptr;};
struct BootHealthRequirements {
  bool reachability = true, platform = true, device_api = true;
};
struct RuntimeHealthObservation { bool reachability = false, platform = false, device_api = false; };
using RuntimeServiceFactory = ServiceNode (*)(DeviceContext &, DeviceProjectionRegistry &);
// Immutable composition inputs, borrowed for the whole boot. Mandatory health
// (application, I/O, required services) cannot be disabled by a preset.
struct RuntimePreset {
  const char *id;
  const RuntimeServiceFactory *services = nullptr;
  size_t service_count = 0;
  const StandardProjectionContribution *const *projections = nullptr;
  size_t projection_count = 0;
  bool (*configure_backends)(std::string &) = nullptr;
  BootHealthRequirements health{};
  RuntimeHealthObservation (*observe_health)(DeviceContext &) = nullptr;
  // Device may create adapter defaults only when the graph has no supplied node.
  // Explicit policy survives copying a preset; it is not inferred from identity.
  bool default_execution_services = false;
  // Explicit backend/executor selection and guest retirement metadata. No VM
  // or task is created by reading these boot-lived contribution declarations.
  std::span<const RuntimeExecutionContribution *const> execution{};
};
const RuntimePreset &default_runtime_preset();
inline const RuntimePreset &selected_runtime_preset(const RuntimePreset *preset) {
  return preset ? *preset : default_runtime_preset();
}
bool validate_runtime_preset(const RuntimePreset &, std::string &error);
}
