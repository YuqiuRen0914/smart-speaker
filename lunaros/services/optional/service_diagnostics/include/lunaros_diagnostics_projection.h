#pragma once
#include "device_api/device_projection.h"
namespace lunaredu::lunaros { DeviceOperationsView diagnostics_device_operations(); void fill_diagnostics_capabilities(DeviceApiContext &, JsonObject); bool install_diagnostics_debug_projection(DeviceProjectionRegistry &,std::string &); }
