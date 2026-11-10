#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
namespace lunaredu::lunaros {
class KernelRuntime;
inline constexpr size_t kNetworkWifiScanCapacity = 24;
inline constexpr size_t kNetworkWifiSsidCapacity = 33;
inline constexpr size_t kNetworkWifiScanErrorCapacity = 48;
enum class NetworkWifiScanState : uint8_t { Idle, Scanning, Ready, Failed };
struct NetworkWifiScanAccessPoint {
  std::array<char, kNetworkWifiSsidCapacity> ssid{};
  int32_t rssi = 0;
  bool secure = false;
  uint8_t authmode = 0;
  uint8_t channel = 0;
  std::array<uint8_t, 6> bssid{};
};
struct NetworkWifiScanSnapshot {
  uint32_t sequence = 0;
  NetworkWifiScanState state = NetworkWifiScanState::Idle;
  std::array<NetworkWifiScanAccessPoint, kNetworkWifiScanCapacity> access_points{};
  size_t access_point_count = 0;
  std::array<char, kNetworkWifiScanErrorCapacity> error{};
};
NetworkWifiScanSnapshot network_wifi_scan_snapshot();
void network_wifi_scan_snapshot_into(NetworkWifiScanSnapshot &snapshot);
bool request_network_wifi_scan(KernelRuntime &runtime, std::string &error);
// Only a foreign caller may wait; never a service executor or job runner.
// Timeout releases the waiter, not the shared scan owned by its job.
bool request_network_wifi_scan_and_wait(KernelRuntime &runtime,
    NetworkWifiScanSnapshot &snapshot, std::string &error,
    uint32_t timeout_ms = 15000);
} // namespace lunaredu::lunaros
