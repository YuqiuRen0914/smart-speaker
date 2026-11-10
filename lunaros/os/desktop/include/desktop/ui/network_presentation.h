#pragma once
#include <cstdint>
#include <string>
#include <string_view>
namespace lunaredu::lunaros {
enum class NetworkIndicatorState {
  Disconnected,
  Connecting,
  Provisioning,
  Connected,
};

struct NetworkIndicator {
  NetworkIndicatorState state = NetworkIndicatorState::Disconnected;
  int rssi = 0;  // Zero means unavailable, never excellent signal.
};

int network_signal_bars(const NetworkIndicator &network);

std::string_view provisioning_ssid_suffix(std::string_view ssid);

std::string provisioning_caption(uint32_t current_ms,
                                        std::string_view ssid,
                                        std::string_view password,
                                        std::string_view ip);

} // namespace lunaredu::lunaros
