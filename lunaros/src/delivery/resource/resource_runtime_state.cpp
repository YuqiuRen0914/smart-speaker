#include "delivery/resource/resource_runtime_internal.h"

#include <esp_timer.h>
#include <esp_attr.h>

namespace lunaredu::lunaros::internal {

ResourceRuntimeState &resource_runtime_state() {
  // Large fixed records live in PSRAM where supported. Synchronization and
  // runtime control remain internal; no lazy heap allocation or failure path.
  static EXT_RAM_BSS_ATTR ResourceDeliveryRegistry::Entries entries{};
  static ResourceRuntimeState state(entries);
  return state;
}

uint32_t resource_now_ms() {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

} // namespace lunaredu::lunaros::internal
