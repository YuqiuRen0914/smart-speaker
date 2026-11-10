#pragma once
#include <array>
#include <cstdint>
#include <string>
namespace lunaredu::lunaros {
class KernelRuntime;
struct NetworkSavedWifi {
  std::string ssid;
  bool secure = false;
};
struct NetworkSavedWifiSnapshot {
  std::array<NetworkSavedWifi, 5> credentials;
  size_t count = 0;
  bool configured = false, fixed = false;
  uint8_t last_success_index = 0;
};
NetworkSavedWifiSnapshot network_saved_wifi_snapshot();
enum class NetworkWifiConnectionPhase : uint8_t {
  Idle,
  Connecting,
  Saving,
  Connected,
  ConnectedUnsaved,
  Failed,
  Cancelled
};
constexpr const char *
network_wifi_connection_phase_name(NetworkWifiConnectionPhase s) {
  switch (s) {
  case NetworkWifiConnectionPhase::Idle:
    return "waiting";
  case NetworkWifiConnectionPhase::Connecting:
    return "connecting";
  case NetworkWifiConnectionPhase::Saving:
    return "saving";
  case NetworkWifiConnectionPhase::Connected:
    return "connected";
  case NetworkWifiConnectionPhase::ConnectedUnsaved:
    return "connected_unsaved";
  case NetworkWifiConnectionPhase::Failed:
    return "failed";
  case NetworkWifiConnectionPhase::Cancelled:
    return "cancelled";
  }
  return "unknown";
}
struct NetworkWifiConnectionSnapshot {
  uint32_t attempt = 0, sequence = 0, started_ms = 0;
  NetworkWifiConnectionPhase phase = NetworkWifiConnectionPhase::Idle;
  std::string ssid, ip, error;
};
enum class NetworkWifiCredentialResult {
  Ok,
  MissingSsid,
  NotReady,
  SaveFailed,
  ApplyFailed,
  Busy,
  InvalidCredentials
};
NetworkWifiConnectionSnapshot network_wifi_connection_snapshot();
NetworkWifiCredentialResult
request_network_connect_saved_wifi(KernelRuntime &, const char *ssid,
                                   uint32_t &attempt);
NetworkWifiCredentialResult request_network_connect_wifi(KernelRuntime &,
                                                         const char *ssid,
                                                         const char *password,
                                                         uint32_t &attempt);
bool request_network_cancel_wifi_connection(KernelRuntime &, uint32_t attempt,
                                            std::string &);
bool request_network_forget_wifi(KernelRuntime &, const char *ssid,
                                 std::string &);
bool request_network_clear_saved_wifi(KernelRuntime &, std::string &);
} // namespace lunaredu::lunaros
