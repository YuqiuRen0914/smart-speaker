#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include "lunaros_micropython_runtime.h"
#include "lunaros_micropython_machine.h"


namespace lunaredu::lunaros {
class DeviceProjectionRegistry;
struct DeviceApiContext;
}
namespace lunaredu::app { class CapabilityRegistry; }

namespace lunaredu::micropython {

// The explicit OS switch is authoritative for default and product-provided
// runtime nodes, projections and application backends alike.
bool core_enabled();
// Capability availability is distinct from the lazily allocated VM instance.
bool runtime_configured();

struct RobotMotionStatus {
  bool active = false;
  bool moving = false;
  float left_speed = 0, right_speed = 0, pan = 0, tilt = 0;
};
struct RobotMotionLimits {
  float maximum_speed = 0, default_speed = 0;
  uint32_t minimum_duration_ms = 0, maximum_duration_ms = 0, default_duration_ms = 0;
  constexpr bool valid() const {
    return maximum_speed > 0 && maximum_speed <= 1 && default_speed >= 0 &&
        default_speed <= maximum_speed && minimum_duration_ms > 0 &&
        minimum_duration_ms <= default_duration_ms &&
        default_duration_ms <= maximum_duration_ms && maximum_duration_ms <= INT32_MAX;
  }
};
struct RobotMotionPort {
  bool (*status)(app::CapabilityRegistry &, RobotMotionStatus &, std::string &) = nullptr;
  bool (*execute)(app::CapabilityRegistry &, const char *, const char *,
                  std::string &, std::string &) = nullptr;
  // Product-owned physical limits and defaults. Missing/invalid limits prevent
  // registration of movement bindings; VM code contains no physical policy.
  RobotMotionLimits limits;
};

struct RuntimeConfig {
  bool install_hardware_bindings = false;
  // Keep the service and projection available without creating the VM owner
  // task. The first foreground entry or mutating MicroPython API call starts
  // the runtime. Idle application exits and explicit service stop can reclaim
  // the VM; enabling this service never runs user code by itself.
  bool start_on_demand = false;
  // Immutable product policy, checked on the VM task before hardware mutation.
  // A null policy keeps standalone hardware lessons unrestricted.
  bool (*hardware_access)(const char *capability) = nullptr;
  // Optional robot binding supplied by the actuator-bearing product.
  RobotMotionPort robot_motion;
  // Optional, product-selected lesson I/O. Never infer free pins from geometry.
  MachinePort machine;
  // Main-owner handoff for product business workers not owned by an AppController.
  void *foreground_context = nullptr;
  bool (*foreground_transition)(void *, bool active, std::string &error) = nullptr;
  // Filled by runtime_lifecycle_node; the config must keep its boot lifetime.
  lunaredu::lunaros::DeviceApiContext *owner_device = nullptr;
};

bool hardware_binding_access_allowed(const char *capability);
const RobotMotionPort *runtime_robot_motion_port();

// Starts a configured on-demand runtime, or returns immediately when the VM is
// already ready. The runtime service must have completed its normal service
// startup before this function is called.
// Runtime startup never selects or executes user scripts.
bool ensure_runtime_started(std::string &error);
// Background admission can reclaim only a VM started by this attempt. The
// claim also survives readiness timeout so a late owner can be retired safely.
bool ensure_runtime_started(std::string &error, MicroPythonStartupClaim &claim);
// Admit one managed, transient startup worker. Never waits for VM startup on
// the caller's realtime/UI loop. true means ready or preparation admitted.
bool prepare_runtime_async(std::string &error);
// Includes the admitted preparation task until its managed wrapper retires.
// A foreground exit must keep its lease while a late startup can still create a VM.
bool runtime_preparation_pending();
bool transition_foreground(bool active, std::string &error);

// Execution configuration and shutdown; no ServiceHost registration.
bool configure_runtime(RuntimeConfig &, std::string &);
bool stop_configured_runtime(RuntimeConfig &, std::string &);

// The Python App installs its control surface before service startup. Runtime
// configuration publishes service availability; a dormant or failed VM does
// not close its own startup/retry entrypoint. VM readiness is reported by status.
struct RuntimeControlSurface {
  bool (*set_available)(bool, std::string &) = nullptr;
  const char *(*foreground_application)(const lunaredu::lunaros::DeviceApiContext &) = nullptr;
};
void configure_runtime_control_surface(RuntimeControlSurface);
std::string runtime_foreground_application(const lunaredu::lunaros::DeviceApiContext &);
lunaredu::lunaros::DeviceApiContext *runtime_device_context();
} // namespace lunaredu::micropython
