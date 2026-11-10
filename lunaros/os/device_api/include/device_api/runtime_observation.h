#pragma once
#include <array>
#include <cstdint>
namespace lunaredu::lunaros {
inline constexpr std::array<const char *, 7> kBootstrapStageNames{
    "core_runtime", "configuration", "network", "service_registration",
    "device_projections", "executor_tasks", "services"};
struct BootstrapMetrics {
  bool available=false, completed=false, succeeded=false;
  uint32_t total_ms=0;
  std::array<uint32_t, kBootstrapStageNames.size()> stage_ms{};
};
// Immutable boot-health facts for diagnostic and update consumers. Lifecycle
// mutation remains in the bootstrap owner.
struct BootHealthObservation {
  enum Block : uint32_t {
    Application = 1, Io = 2, RequiredServices = 4, Reachability = 8,
    Platform = 16, DeviceApi = 32, Bootstrap = 64, Observed = 128,
  };
  uint32_t bits = 0;
};
} // namespace lunaredu::lunaros
