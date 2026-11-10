#pragma once
#include <lunaros_network_local_access.h>
namespace lunaredu::lunaros {
bool open_network_local_access(KernelRuntime &,const NetworkLocalAccessConfig &,NetworkLocalAccessLease &,std::string &);
bool close_network_local_access(std::string &);
bool release_network_local_access(NetworkLocalAccessLease,std::string &);
NetworkLocalAccessState network_local_access_state_on_owner();
const char *network_local_access_ssid();
bool network_local_access_active();
void tick_network_local_access();
bool set_network_bluetooth_on_owner(KernelRuntime &,bool,std::string &);
bool stop_network_bluetooth_on_owner(std::string &);
}
