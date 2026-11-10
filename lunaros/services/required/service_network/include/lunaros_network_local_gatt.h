#pragma once

#include "lunaros_device_protocol.h"

#include <string>

namespace lunaredu::lunaros {

// Stable UUIDs shared by the firmware and the Tools Web Bluetooth adapter.
inline constexpr const char *kNetworkLocalGattServiceUuid =
    "f03c9ea1-4d77-4f2d-906a-704c425a0101";
inline constexpr const char *kNetworkLocalGattRxCharacteristicUuid =
    "f03c9ea1-4d77-4f2d-906a-704c425a0102";
inline constexpr const char *kNetworkLocalGattTxCharacteristicUuid =
    "f03c9ea1-4d77-4f2d-906a-704c425a0103";

using NetworkLocalGattFrameHandler = bool (*)(const DeviceFrame &request,
                                              DeviceFrame &response,
                                              void *user_context);

struct NetworkLocalGattConfig {
  const char *device_name_prefix = "LunarOS";
  NetworkLocalGattFrameHandler frame_handler = nullptr;
  void *user_context = nullptr;
  void (*session_closed)(void *) = nullptr;
};

enum class NetworkLocalGattState {
  Stopped,
  WaitingForHost,
  Starting,
  Advertising,
  Connected,
  Stopping,
  Failed
};
struct NetworkLocalGattSnapshot {
  NetworkLocalGattState state = NetworkLocalGattState::Stopped;
  std::string error;
  uint32_t rejected_writes = 0;
  uint32_t decode_errors = 0;
  uint32_t notify_retries = 0;
  uint32_t response_timeouts = 0;
};

// Single product/network owner. start accepts asynchronous host startup;
// snapshot/running report actual readiness. Poll from that same owner's loop
// to reconcile host callbacks and retry failures at most once per 250 ms.
bool network_local_gatt_start(const NetworkLocalGattConfig &config,
                              std::string &error);
bool network_local_gatt_poll(std::string &error);
NetworkLocalGattSnapshot network_local_gatt_snapshot();
// A pending disconnect returns false; poll/repeated stop finish cleanup.
bool network_local_gatt_stop(std::string &error);
bool network_local_gatt_running();
// Register the profile before an idle Bluetooth host starts, without
// advertising.
bool network_local_gatt_prepare(const NetworkLocalGattConfig &,
                                std::string &error);
bool network_local_gatt_connected();
bool network_local_gatt_supported();

} // namespace lunaredu::lunaros
