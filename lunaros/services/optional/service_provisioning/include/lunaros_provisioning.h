#pragma once
#include "lunaros_provisioning_status.h"
#include "device_api/device_projection.h"
#include "kernel/service.h"
#include "lunaros_network_wifi_configuration.h"
#include <string>
namespace lunaredu::lunaros {
inline constexpr const char *kProvisioningServiceId = "system.provisioning";
bool configure_provisioning_runtime(KernelRuntime &, const lunaredu::lunaros::ProvisioningPolicy &,
                                    std::string &);
ServiceNode provisioning_service_node(DeviceApiContext &);
bool declare_provisioning_projection(DeviceProjectionRegistry &, std::string &);
void poll_provisioning_on_owner();
ProvisioningSnapshot provisioning_snapshot();
ProvisioningFastSnapshot provisioning_fast_snapshot();
ProvisioningProgress provisioning_progress();
bool request_provisioning_begin(KernelRuntime &, lunaredu::lunaros::ProvisioningMode,
                                bool suspend_station, std::string &);
bool request_provisioning_end(KernelRuntime &, std::string &);
bool request_provisioning_cancel_attempt(KernelRuntime &, uint32_t,
                                         std::string &);
NetworkWifiCredentialResult submit_provisioning_saved_wifi(KernelRuntime &,
                                                           const char *ssid);
NetworkWifiCredentialResult
submit_provisioning_wifi_credentials(KernelRuntime &, const char *,
                                     const char *);
// Local presentation only; never include these secrets in device projections
// or transport responses. Activity reads additionally enforce caller ownership.
std::string provisioning_local_password();
std::string provisioning_local_url();
const char *provisioning_mode_name(lunaredu::lunaros::ProvisioningMode);
} // namespace lunaredu::lunaros
