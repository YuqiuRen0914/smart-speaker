#include "runtime/preset.h"
#include "runtime/device_context.h"
#include "kernel/runtime.h"
#include "lunaros_network.h"
#include "lunaros_network_lifecycle.h"
#include "lunaros_device_api_service_id.h"
namespace lunaredu::lunaros {
RuntimeHealthObservation observe_default_boot_health(DeviceContext &device) {
  RuntimeHealthObservation result;
  result.device_api = device.runtime.service_available(kDeviceApiServiceId);
  if (!network_runtime_owned_by(device.runtime)) return result;
  const auto network = network_fast_snapshot();
  result.reachability = network.link == NetworkLinkState::Up ||
      (network.local_access == NetworkLocalAccessState::Active && network.local_access_has_address);
  result.platform = network.platform_ready;
  return result;
}
}
