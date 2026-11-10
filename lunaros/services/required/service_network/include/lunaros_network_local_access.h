#pragma once
#include "lunaros_network_local_gatt.h"
#include "lunaros_network.h"
#include <string>
namespace lunaredu::lunaros {
class KernelRuntime;
constexpr const char *
network_local_access_state_name(NetworkLocalAccessState s) {
  switch (s) {
  case NetworkLocalAccessState::Idle:
    return "idle";
  case NetworkLocalAccessState::Active:
    return "active";
  case NetworkLocalAccessState::Preparing:
    return "preparing";
  case NetworkLocalAccessState::Stopping:
    return "stopping";
  }
  return "unknown";
}
struct NetworkLocalAccessLease {
  uint32_t generation = 0;
};
struct NetworkLocalAccessConfig {
  std::string ssid, password;
  bool suspend_station = false, ble = false;
  NetworkLocalGattConfig gatt;
};
bool request_network_open_local_access(KernelRuntime &,
                                       const NetworkLocalAccessConfig &,
                                       NetworkLocalAccessLease &,
                                       std::string &);
bool request_network_close_local_access(KernelRuntime &,
                                        NetworkLocalAccessLease, std::string &);
bool request_network_prepare_local_gatt(KernelRuntime &,
                                        const NetworkLocalGattConfig &,
                                        std::string &);
} // namespace lunaredu::lunaros
