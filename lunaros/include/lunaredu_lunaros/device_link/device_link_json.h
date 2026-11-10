#pragma once
#include <ArduinoJson.h>
#include "lunaredu_lunaros/device_link/device_link.h"

namespace lunaredu::lunaros {
// Explicit cloud codec boundary. Callbacks borrow their inputs and must not retain
// JSON views. They run synchronously on the calling owner, outside OS locks;
// implementations synchronize their snapshots and create no tasks/network jobs.
// Registration is immutable and must outlive the DeviceLink service.
struct DeviceLinkObserver {
  void (*write_runtime)(JsonObject) = nullptr;
  void (*binding_changed)(const DeviceLinkPairingSnapshot &) = nullptr;
  void (*restore)(const DeviceLinkPairingSnapshot &) = nullptr;
  void (*presence)(const DeviceLinkPairingSnapshot &, JsonVariantConst) = nullptr;
};
}
