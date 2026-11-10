#pragma once
#include "kernel/product_definition.h"

namespace lunaredu::lunaros {
namespace apps { class ApplicationsService; }
class DeviceProjectionRegistry;
struct DeviceContext;
struct RuntimePreset;

// Common services and standard projections are installed once for both Device
// and products with dedicated executors. The definition contains additions only.
// An explicit service with a default service ID replaces that default; duplicate
// product IDs are rejected. Installation and its reset callbacks are synchronous.
// Callback contexts and dependency arrays must outlive the installed runtime.
bool install_product_composition(DeviceContext &device, DeviceProjectionRegistry &projections,
                                 apps::ApplicationsService &applications,
                                 const ProductDefinition &product,
                                 std::string &error);
// Explicit preset replaces the default graph; ApplicationsService remains the
// framework maintenance owner. Inputs are validated before factories run.
bool install_product_composition(DeviceContext &, DeviceProjectionRegistry &,
    apps::ApplicationsService &, const ProductDefinition &, std::string &, const RuntimePreset &);
}
