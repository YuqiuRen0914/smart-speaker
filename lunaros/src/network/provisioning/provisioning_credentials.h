#pragma once

#include <stddef.h>

#include "lunaredu_lunaros/persistence/settings.h"
#include "network/provisioning/network_provisioning.h"

namespace lunaredu::lunaros {

// Build the persisted credential order after a provisioning submission. The
// submitted credential is always first; saved credentials retain their order
// and duplicate/blank SSIDs are discarded.
size_t build_provisioning_wifi_credentials(
    const DeviceWifiCredential &submitted,
    const DeviceWifiCredential *saved_credentials, size_t saved_count,
    DeviceWifiCredential *output, size_t output_capacity);

NetworkProvisioningCredentialResult apply_provisioning_wifi_credentials(
    const char *ssid, const char *password);

} // namespace lunaredu::lunaros
