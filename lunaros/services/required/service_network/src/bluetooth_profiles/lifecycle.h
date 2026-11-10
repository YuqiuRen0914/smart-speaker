#pragma once
#include <string>
namespace lunaredu::lunaros {
class KernelRuntime;
bool start_network_bluetooth_profiles(KernelRuntime &,std::string &);
void stop_network_bluetooth_profiles();
bool reset_network_bluetooth_profiles(std::string &);
}
