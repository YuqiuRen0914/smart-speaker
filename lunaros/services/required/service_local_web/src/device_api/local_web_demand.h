#pragma once
#include <string>
namespace lunaredu::lunaros {
class DeviceProjectionRegistry;
namespace web_internal {
bool demand_enabled();
bool demand_start(DeviceProjectionRegistry &, std::string &);
bool demand_stop(std::string &);
bool start_full(DeviceProjectionRegistry &, std::string &);
bool stop_full(std::string &);
}
}
