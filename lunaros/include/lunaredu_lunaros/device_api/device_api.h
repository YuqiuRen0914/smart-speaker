#pragma once

#include <cstdint>
#include <cstddef>
#include <string>

namespace lunaredu::lunaros {

struct ServiceNode;

enum class DeviceHostConnectionState : uint8_t {
  Disconnected,
  Connected,
  Error,
  Lan,
};

struct DeviceHostConnectionSnapshot {
  DeviceHostConnectionState state = DeviceHostConnectionState::Disconnected;
  uint32_t last_activity_ms = 0;
};

// Register the stable local HTTP surface before it is exposed. This does not
// open a socket or create an additional runtime task.
void setup_device_api();

// Open the device-local HTTP control surface after the ESP32 network stack is
// ready. The ESP-IDF HTTP server owns its existing internal worker task.
bool start_device_api(std::string &error);

// Stop the HTTP transport without discarding the immutable route catalog.
bool stop_device_api(std::string &error);

// Host transports report bounded activity here. The built-in Local Device API
// reports private-network requests as Lan; Connected is reserved for a live
// non-LAN transport such as USB.
void report_device_host_connection(DeviceHostConnectionState state);
DeviceHostConnectionSnapshot device_host_connection_snapshot();

// Register Device API as a shared service that starts only after Network is
// ready. No product-specific callback is needed.
ServiceNode device_api_service_node();

} // namespace lunaredu::lunaros
