#pragma once

#include <stdint.h>

inline constexpr uint32_t kWifiDhcpLeaseWaitMs = 15000;

struct WifiReconnectInput {
  uint32_t now_ms = 0;
  uint32_t last_reconnect_ms = 0;
  uint32_t reconnect_attempts = 0;
  uint32_t station_connected_since_ms = 0;
  bool connected = false;
  bool station_suspended = false;
  bool credentials_available = false;
  bool station_connected = false;
  bool connection_in_progress = false;
  bool has_connected_once = false;
};

struct WifiReconnectDecision {
  uint32_t reconnect_delay_ms = 0;
  uint32_t station_connected_elapsed_ms = 0;
  bool waiting_for_dhcp = false;
  bool dhcp_wait_expired = false;
  bool should_reconnect = false;
  bool should_advance_credential = false;
};

uint32_t wifi_reconnect_delay_for_attempts(uint32_t attempts);
WifiReconnectDecision
decide_wifi_reconnect(const WifiReconnectInput &input);
