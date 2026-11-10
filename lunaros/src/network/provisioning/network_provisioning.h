#pragma once

#include <stdint.h>

#include "lunaredu_lunaros/network/network_provisioning.h"

namespace lunaredu::lunaros {

const char *network_provisioning_softap_ssid();
bool start_network_provisioning_softap_only();
void stop_network_provisioning_softap_only();

bool start_network_provisioning(NetworkProvisioningMode mode);
void start_network_provisioning();
void stop_network_provisioning();
bool network_provisioning_active();
void tick_network_provisioning();

} // namespace lunaredu::lunaros
