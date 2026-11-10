#pragma once
#include <array>
#include <cstdint>
#include <mutex>
namespace lunaredu::lunaros {
struct DeviceApiContext;
enum class DeviceHostConnectionState : uint8_t { Disconnected, Connected, Error, Lan };
enum class DeviceHostTransport : uint8_t { Usb, LocalWeb };
struct DeviceHostConnectionSnapshot {
  DeviceHostConnectionState state = DeviceHostConnectionState::Disconnected;
  uint32_t last_activity_ms = 0;
};
// Device-owned, coherent records. One transport cannot withdraw another's state.
class DeviceHostConnectionTracker {
public:
  explicit DeviceHostConnectionTracker(DeviceApiContext &);
  ~DeviceHostConnectionTracker();
  DeviceHostConnectionTracker(const DeviceHostConnectionTracker &) = delete;
  DeviceHostConnectionTracker &operator=(const DeviceHostConnectionTracker &) = delete;
  static DeviceHostConnectionTracker *bound(const DeviceApiContext &);
private:
  friend void report_device_host_connection(DeviceApiContext &, DeviceHostTransport, DeviceHostConnectionState);
  friend DeviceHostConnectionSnapshot device_host_connection_snapshot(const DeviceApiContext &);
  mutable std::mutex mutex_;
  std::array<DeviceHostConnectionSnapshot, 2> transports_{};
  DeviceApiContext &owner_;
};
void report_device_host_connection(DeviceApiContext &, DeviceHostTransport, DeviceHostConnectionState);
DeviceHostConnectionSnapshot device_host_connection_snapshot(const DeviceApiContext &);
}
