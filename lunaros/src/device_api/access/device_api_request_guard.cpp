#include "device_api/access/device_api_request_guard.h"

#include "lunaredu_lunaros/network/network.h"

bool device_api_provisioning_surface_active() {
  const auto snapshot = lunaredu::lunaros::network_snapshot();
  return snapshot.provisioning !=
             lunaredu::lunaros::NetworkProvisioningState::Idle &&
         (!snapshot.provisioning_ssid.empty() ||
          !snapshot.provisioning_ip.empty());
}

bool device_api_current_request_allowed(DeviceApiAccessSurface surface,
                                       const char *operation) {
  return device_api_access_policy_allows(device_api_provisioning_surface_active(),
                                        surface, operation);
}
