#pragma once

#include "device_api/access/device_api_access_policy.h"

bool device_api_provisioning_surface_active();

bool device_api_current_request_allowed(DeviceApiAccessSurface surface,
                                       const char *operation = nullptr,
                                       const char *admin_grant = nullptr);
