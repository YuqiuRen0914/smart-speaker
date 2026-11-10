#pragma once

#include <stddef.h>

#include "configuration/device_config.h"

namespace lunaredu::lunaros {

class KernelRuntime;

// Build the persisted credential order after a successful connection. The
// submitted credential is always first; saved credentials retain their order
// and duplicate/blank SSIDs are discarded.
size_t build_wifi_credential_list(
    const DeviceWifiCredential &submitted,
    const DeviceWifiCredential *saved_credentials, size_t saved_count,
    DeviceWifiCredential *output, size_t output_capacity);

} // namespace lunaredu::lunaros
