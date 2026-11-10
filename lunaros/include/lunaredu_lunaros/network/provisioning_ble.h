#pragma once

#include "lunaredu_lunaros/network/provisioning_protocol.h"

#include <string>

namespace lunaredu::lunaros {

// Stable UUIDs shared by the firmware and the Tools Web Bluetooth adapter.
inline constexpr const char *kProvisioningBleServiceUuid =
    "f03c9ea1-4d77-4f2d-906a-704c425a0101";
inline constexpr const char *kProvisioningBleRxCharacteristicUuid =
    "f03c9ea1-4d77-4f2d-906a-704c425a0102";
inline constexpr const char *kProvisioningBleTxCharacteristicUuid =
    "f03c9ea1-4d77-4f2d-906a-704c425a0103";

using ProvisioningBleFrameHandler = bool (*)(const ProvisioningFrame &request,
                                              ProvisioningFrame &response,
                                              void *user_context);

struct ProvisioningBleConfig {
  const char *device_name_prefix = "LunarOS";
  ProvisioningBleFrameHandler frame_handler = nullptr;
  void *user_context = nullptr;
};

enum class ProvisioningBleState { Stopped, WaitingForHost, Starting, Advertising, Connected, Stopping, Failed };
struct ProvisioningBleSnapshot {
  ProvisioningBleState state = ProvisioningBleState::Stopped;
  std::string error;
};

// Single product/network owner. start accepts asynchronous host startup;
// snapshot/running report actual readiness. Poll from that same owner's loop
// to reconcile host callbacks and retry failures at most once per 250 ms.
bool provisioning_ble_start(const ProvisioningBleConfig &config,
                            std::string &error);
bool provisioning_ble_poll(std::string &error);
ProvisioningBleSnapshot provisioning_ble_snapshot();
// A pending disconnect returns false; poll/repeated stop finish cleanup.
bool provisioning_ble_stop(std::string &error);
bool provisioning_ble_running();

}  // namespace lunaredu::lunaros
