#pragma once
#include <vector>
#include "runtime/preset.h"
#include "device_api/projection_contribution.h"
#include "kernel/product_definition.h"

namespace lunaredu::app { class AppController; }
namespace lunaredu::lunaros {
namespace apps { class ApplicationsService; }
class DeviceProjectionRegistry;
struct DeviceContext;
RuntimeHealthObservation observe_default_boot_health(DeviceContext &);
// Actual default graph and declarations, extended by DeviceOptions at the root.
std::vector<ServiceNode> default_service_nodes(
    DeviceContext &device, DeviceProjectionRegistry &projections, apps::ApplicationsService &applications,
    const ServiceDependency *engines = nullptr, size_t engine_count = 0);
bool declare_default_projections(void *, std::string &error);
bool register_default_foreground(app::AppController &, const char *name, std::string &error);
} // namespace lunaredu::lunaros
