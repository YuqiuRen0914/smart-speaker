#pragma once

namespace lunaredu::app { class CapabilityRegistry; }
namespace lunaredu::lunaros { struct DeviceApiContext; }

namespace lunaredu::micropython {

// Borrowed from the product-owned runtime service configuration. Native VM
// bindings use this explicit owner instead of process-wide device registries.
lunaredu::lunaros::DeviceApiContext *runtime_device_context();
lunaredu::app::CapabilityRegistry *runtime_application_capabilities();

} // namespace lunaredu::micropython
