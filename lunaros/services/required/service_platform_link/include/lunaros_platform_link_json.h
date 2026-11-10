#pragma once
#include <ArduinoJson.h>
#include "lunaros_platform_link.h"

namespace lunaredu::lunaros {
// Explicit cloud codec boundary. Callbacks borrow their inputs and must not retain
// JSON views. They run synchronously on the calling owner, outside OS locks;
// implementations synchronize their snapshots and create no tasks/network jobs.
// Registration is immutable and must outlive the PlatformLink service.
struct PlatformLinkObserver {
  void *context = nullptr;
  void (*write_runtime)(void *, JsonObject) = nullptr;
  void (*binding_changed)(const PlatformLinkPairingSnapshot &) = nullptr;
  void (*restore)(const PlatformLinkPairingSnapshot &) = nullptr;
  void (*presence)(const PlatformLinkPairingSnapshot &, JsonVariantConst) = nullptr;
  // Return true only after the product accepted the event into its own
  // presentation queue. Replayed IDs must be handled idempotently.
  bool (*notification)(const PlatformLinkNotification &) = nullptr;
};
}
