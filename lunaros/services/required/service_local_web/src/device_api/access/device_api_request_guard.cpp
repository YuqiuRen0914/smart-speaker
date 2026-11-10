#include "device_api/access/device_api_request_guard.h"

#if LUNAROS_LOCAL_WEB_PROVISIONING_COMPONENT
#include "lunaros_provisioning.h"
#endif
#include "lunaredu_local_web_portal.h"
#include "device_api/access/device_api_bound_access.h"

bool device_api_provisioning_surface_active() {
#if LUNAROS_LOCAL_WEB_PROVISIONING_COMPONENT
  return lunaredu::lunaros::provisioning_fast_snapshot().surface_active;
#else
  return false;
#endif
}

bool device_api_current_request_allowed(DeviceApiAccessSurface surface,
                                       const char *operation,
                                       const char *admin_grant) {
  if (surface == DeviceApiAccessSurface::Operation &&
      !lunaredu::lunaros::device_web_operation_permitted(operation)) return false;
  if (admin_grant != nullptr && device_api_admin_access_authorized(admin_grant))
    return true;
  return (surface == DeviceApiAccessSurface::Operation &&
          lunaredu::lunaros::device_web_local_operation_allowed(operation)) ||
         device_api_access_policy_allows(device_api_provisioning_surface_active(),
                                        surface, operation);
}
