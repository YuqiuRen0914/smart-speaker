#pragma once
#include "lunaros_network_scan.h"

namespace lunaredu::lunaros {
// A survey keeps BSSID identity and hidden APs; Settings' connection scan stays
// unchanged. Storage is bounded; total_count makes truncation explicit.
struct NetworkWifiSurveySnapshot {
  NetworkWifiScanSnapshot scan{};
  uint16_t total_count = 0;
};
bool request_network_wifi_survey(KernelRuntime &, std::string &error);
void network_wifi_survey_snapshot_into(NetworkWifiSurveySnapshot &);

struct NetworkWifiMonitorHandle {
  uint32_t value = 0;
  explicit operator bool() const { return value != 0; }
};
struct NetworkWifiActivitySnapshot {
  uint32_t sequence = 0, session = 0, elapsed_ms = 0;
  bool active = false;
  uint8_t channel = 0;
  uint32_t management = 0, control = 0, data = 0;
};
// Optional App-owned radio work, run on the Network executor. The context and
// callbacks must remain alive until checked stop succeeds. stop is retried on
// failure, including failed prepare, expiry and service revocation. No App
// implementation or signal-processing state is owned by Network.
struct NetworkWifiMonitorClient {
  void *context = nullptr;
  bool (*prepare)(void *, uint8_t channel, std::string &) = nullptr;
  void (*poll)(void *, uint8_t channel) = nullptr;
  bool (*stop)(void *, std::string &) = nullptr;
  // Refuse STA/reconnect, ESP-NOW and enabled Bluetooth. Network temporarily
  // disables power saving and restores channel/PHY/power policy on checked stop.
  bool exclusive = false;
};
// Current-channel observation preserves associations. A different channel is
// admitted only by the shared radio policy. Never disconnects or transmits.
// Callers renew within five seconds; stop is handle scoped and idempotent.
bool request_network_wifi_monitor_start(KernelRuntime &, uint8_t channel,
    NetworkWifiMonitorHandle &, std::string &error);
bool request_network_wifi_monitor_start(KernelRuntime &, uint8_t channel,
    const NetworkWifiMonitorClient &, NetworkWifiMonitorHandle &, std::string &error);
bool request_network_wifi_monitor_renew(KernelRuntime &, NetworkWifiMonitorHandle,
    std::string &error);
bool request_network_wifi_monitor_stop(KernelRuntime &, NetworkWifiMonitorHandle,
    std::string &error);
void network_wifi_activity_snapshot_into(NetworkWifiActivitySnapshot &);
} // namespace lunaredu::lunaros
