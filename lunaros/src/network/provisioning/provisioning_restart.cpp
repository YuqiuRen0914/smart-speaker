#include "network/provisioning/provisioning_restart.h"

namespace lunaredu::lunaros {

uint32_t normalize_provisioning_restart_delay(uint32_t requested_delay_ms) {
  return requested_delay_ms < kProvisioningMinimumRestartDelayMs
             ? kProvisioningMinimumRestartDelayMs
             : requested_delay_ms;
}

uint32_t ProvisioningRestartSchedule::schedule(uint32_t now_ms,
                                               uint32_t requested_delay_ms) {
  const uint32_t delay_ms =
      normalize_provisioning_restart_delay(requested_delay_ms);
  pending_ = true;
  due_at_ms_ = now_ms + delay_ms;
  return delay_ms;
}

bool ProvisioningRestartSchedule::consume_if_due(uint32_t now_ms) {
  if (!pending_ || static_cast<int32_t>(now_ms - due_at_ms_) < 0) {
    return false;
  }

  pending_ = false;
  return true;
}

} // namespace lunaredu::lunaros
