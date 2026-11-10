#pragma once
#include <cstdint>
#include <string>

namespace lunaredu::lunaros {
enum class BootGuardOwner : uint8_t { Main, Network };
struct BootGuardSnapshot {
  bool enabled = false, watchdog_active = false, rescue_requested = false;
  uint32_t consecutive_failures = 0, rescue_attempts = 0;
  uint32_t reset_reason = 0;
  std::string error;
  uint32_t configured_timeout_ms = 0;
  uint64_t continuous_health_ms = 0;
  bool health_committed = false;
  uint32_t health_interruptions = 0, last_blocked_mask = 0;
  uint64_t last_interruption_ms = 0, interrupted_health_ms = 0;
};
// Platform mechanism only. The update owner chooses and validates any rescue
// image; products may retain a management-only composition when none is valid.
using BootGuardRescue = bool (*)(std::string &error, void *context);
bool system_boot_guard_start(BootGuardRescue rescue, void *context,
                             std::string &error);
void system_boot_guard_heartbeat(BootGuardOwner owner);
// Call from the health owner after evaluating actual application + management
// health. A continuous healthy minute clears the crash budget, not image VALID.
void system_boot_guard_observe_health(bool healthy, uint32_t blocked_mask = 0);
BootGuardSnapshot system_boot_guard_snapshot();
}
