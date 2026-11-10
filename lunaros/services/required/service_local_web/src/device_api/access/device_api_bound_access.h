#pragma once
#include <string>

namespace lunaredu::lunaros { struct DeviceOperationSpec; }

// Local management authorization is currently disabled by product policy.
// This does not grant Passport administrator or diagnostic-session authority.
inline constexpr const char *kDeviceAdminOrigin = "https://board.lunaredu.cn";
inline constexpr const char *kDevicePassportAuthorizationUrl = "https://passport.lunaredu.cn/device/authorize";
bool device_api_bound_access_required();
bool device_api_bound_access_authorized(const char *grant);

// Purpose-separated administrator grant, verified even on unbound devices.
// It authorizes all registered local operations, including diagnostics.
bool device_api_admin_access_authorized(const char *grant);
// Non-secret prerequisite status. An empty error means the signed Passport
// launch flow can be attempted; it does not grant authority or promise access.
const char *device_api_admin_readiness_error();


bool device_api_diagnostic_access_authorized(
    const lunaredu::lunaros::DeviceOperationSpec *operation, const char *token);

// Restricted, short-lived sharing ticket; never implies account or administrator authority.
bool device_api_shared_access_authorized(const char *grant);
bool device_api_shared_operation_allowed(const char *module, const char *operation);
bool device_api_shared_control_id_authorized(const char *grant, const char *control_id);
