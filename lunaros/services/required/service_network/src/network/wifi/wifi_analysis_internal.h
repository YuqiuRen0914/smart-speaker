#pragma once
#include "lunaros_network_wifi_analysis.h"

namespace lunaredu::lunaros::internal {
bool wifi_monitor_start(uint8_t, NetworkWifiMonitorHandle &, std::string &);
bool wifi_monitor_start(uint8_t, const NetworkWifiMonitorClient &, NetworkWifiMonitorHandle &, std::string &);
bool wifi_monitor_renew(NetworkWifiMonitorHandle, std::string &);
bool wifi_monitor_stop(NetworkWifiMonitorHandle, std::string &);
bool wifi_monitor_shutdown(std::string &);
bool wifi_monitor_release_exclusive(std::string &);
bool wifi_monitor_active();
void wifi_monitor_poll(uint32_t now_ms);
} // namespace lunaredu::lunaros::internal
