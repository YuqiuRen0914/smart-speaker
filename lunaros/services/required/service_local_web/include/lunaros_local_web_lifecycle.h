#pragma once
#include "lunaros_device_api_service_id.h"
#include <cstdint>
#include <string>
namespace lunaredu::lunaros {
struct ServiceNode;
class DeviceProjectionRegistry;
ServiceNode device_api_service_node(DeviceProjectionRegistry &);
// Product resource policy, set before service startup. Zero keeps HTTP resident
// (e.g. recovery installers). A positive timeout enables a threadless wake
// listener; GET navigation reloads the same URL after HTTP startup.
bool configure_local_web_idle_timeout(uint32_t timeout_ms, std::string &error);
bool local_web_start(DeviceProjectionRegistry &, std::string &error);
bool local_web_stop(std::string &error);
void poll_local_web();
// Read-only boot-health prerequisite, never an access grant. Requires an HTTP
// instance that completed startup and either unrestricted unbound LAN access
// or the same trusted administrator prerequisites used by the launch route.
// Provisioning-only access does not establish a firmware management path.
// Products combine this with service availability and pass it to Device::poll.
bool local_web_management_ready();
}
