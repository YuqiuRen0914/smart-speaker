#pragma once

#include <stdint.h>

namespace lunaredu::lunaros {

inline constexpr uint32_t kProvisioningMinimumRestartDelayMs = 200;

uint32_t normalize_provisioning_restart_delay(uint32_t requested_delay_ms);

class ProvisioningRestartSchedule {
public:
  uint32_t schedule(uint32_t now_ms, uint32_t requested_delay_ms);
  bool consume_if_due(uint32_t now_ms);
  bool pending() const { return pending_; }

private:
  bool pending_ = false;
  uint32_t due_at_ms_ = 0;
};

} // namespace lunaredu::lunaros
