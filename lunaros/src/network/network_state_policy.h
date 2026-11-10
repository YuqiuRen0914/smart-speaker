#pragma once

#include "network/network_service_internal.h"

namespace lunaredu::lunaros::internal {

enum class NetworkProvisioningAction : uint8_t {
  None,
  Start,
  Stop,
};

struct NetworkProvisioningDecision {
  NetworkProvisioningAction action = NetworkProvisioningAction::None;
  NetworkProvisioningMode mode = NetworkProvisioningMode::Disabled;
  bool suspend_station = false;
};

NetworkProvisioningDecision decide_network_provisioning(
    const NetworkProvisioningPolicy &policy,
    const NetworkObservation &observation, bool control_supported);

// Applies driver-owned fields and the link-down internet invalidation rule.
// Lifecycle, device-link and last-error fields remain owned by NetworkService.
bool apply_network_observation(NetworkSnapshot &snapshot,
                               const NetworkObservation &observation);

} // namespace lunaredu::lunaros::internal
