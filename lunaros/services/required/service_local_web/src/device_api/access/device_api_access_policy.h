#pragma once

#include <stdint.h>

enum class DeviceApiAccessSurface : uint8_t {
  Metadata,
  Operation,
};

// The provisioning SoftAP is a deliberately small local surface. The normal
// Local Device API remains available after provisioning, but while the SoftAP is
// active only the explicit basic identity, system-info and network operations
// required by the embedded portal may cross the HTTP boundary.
bool device_api_access_policy_allows(bool provisioning_softap_active,
                                    DeviceApiAccessSurface surface,
                                    const char *operation = nullptr);

// Browser clients may use only the Board environment selected by PlatformLink,
// the device's own embedded page, or the provisioning SoftAP. Origin is not
// account authorization; bound-device operations also require an access grant.
bool device_api_origin_allowed(const char *origin,
                               const char *device_local_ip = nullptr,
                               const char *board_base_url = nullptr);
