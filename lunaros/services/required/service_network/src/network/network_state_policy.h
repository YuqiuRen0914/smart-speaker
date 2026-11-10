#pragma once
#include "lunaros_network.h"

#include "network/network_service_internal.h"

namespace lunaredu::lunaros::internal {

// Applies physical driver observations; STA association is independent of IP
// acquisition, clock synchronization and application server reachability.
// Lifecycle, device-link and last-error fields remain owned by NetworkService.
bool apply_network_observation(NetworkSnapshot &snapshot,
                               const NetworkObservation &observation);

} // namespace lunaredu::lunaros::internal
