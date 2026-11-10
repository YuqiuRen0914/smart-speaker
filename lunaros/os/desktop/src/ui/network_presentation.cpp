#include "desktop/ui/network_presentation.h"

namespace lunaredu::lunaros {
int network_signal_bars(const NetworkIndicator &network) {
  if (network.state != NetworkIndicatorState::Connected || network.rssi >= 0 ||
      network.rssi < -127) {
    return 0;
  }
  return network.rssi >= -55 ? 4
         : network.rssi >= -67 ? 3
         : network.rssi >= -75 ? 2
                               : 1;
}

std::string_view provisioning_ssid_suffix(std::string_view ssid) {
  const size_t separator = ssid.rfind('-');
  if (separator == std::string_view::npos || separator + 1 >= ssid.size()) {
    return ssid;
  }
  return ssid.substr(separator + 1);
}

std::string provisioning_caption(uint32_t current_ms,
                                        std::string_view ssid,
                                        std::string_view password,
                                        std::string_view ip) {
  switch ((current_ms / 3000U) % 3U) {
    case 0:
      return "配网热点 " + std::string(ssid);
    case 1:
      return "密码 " + std::string(password);
    default:
      return "打开 " + std::string(ip.empty() ? "192.168.4.1" : ip);
  }
}
}
