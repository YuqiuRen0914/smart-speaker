#pragma once
#include "lunaros_provisioning_policy.h"
#include <string>
namespace lunaredu::lunaros {
enum class ProvisioningState : uint8_t { Idle, Preparing, Active, Stopping };
constexpr const char *provisioning_state_name(ProvisioningState state) {
  switch (state) {
  case ProvisioningState::Idle:
    return "idle";
  case ProvisioningState::Preparing:
    return "preparing";
  case ProvisioningState::Active:
    return "active";
  case ProvisioningState::Stopping:
    return "stopping";
  }
  return "unknown";
}
struct ProvisioningProgress {
  std::string phase = "waiting", ssid, ip, error;
  uint32_t attempt = 0;
};
struct ProvisioningSnapshot {
  uint64_t session = 0;
  bool expired = false;
  uint32_t sequence = 0, remaining_ms = 0;
  bool started = false, enabled = false, suspends_station = false;
  ProvisioningState state = ProvisioningState::Idle;
  lunaredu::lunaros::ProvisioningMode mode = lunaredu::lunaros::ProvisioningMode::Disabled;
  std::string ssid, ip, error;
  ProvisioningProgress progress;
};
struct ProvisioningFastSnapshot {
  uint32_t sequence = 0;
  ProvisioningState state = ProvisioningState::Idle;
  bool started = false, enabled = false, has_address = false,
       surface_active = false;
};
}
