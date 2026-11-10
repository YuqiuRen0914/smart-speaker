#pragma once

#include "runtime/device_context.h"
#include "runtime/product_runtime.h"
#include "lunaros_network.h"
#include "kernel/product_definition.h"
#include "kernel/runtime.h"

namespace lunaredu::lunaros {
class DeviceProjectionRegistry;
// Device implementation details. Products use runtime/device.h.
// Main owns admission and lifecycle upkeep. Network jobs/executor requests use
// netio; application maintenance has one separately admitted, OTA-accounted lane.
// Call serially on the composition owner. Idempotent for the same runtime
// once ready. Idle workers retire and are readmitted through the stable bridge.
// A wait timeout leaves the admitted startup in progress;
// retry joins that attempt. Creation/bind failure permits a fresh admission.
bool start_maintenance_io(KernelRuntime &runtime, apps::ApplicationsService &applications, std::string &error);
// Composition-owner poll admits due work and observes wrapper retirement.
// No blocking I/O, task join or thread-stack capture occurs on this path.
void poll_maintenance_io(KernelRuntime &runtime);
bool maintenance_io_owned_by(const KernelRuntime &runtime);
bool product_runtime_io_healthy(const KernelRuntime &runtime);
inline constexpr uint32_t kRuntimeUpkeepIntervalMs = 20;
struct MaintenanceState {
  uint32_t last_network_poll = 0;
  bool upkeep_polled = false;
  bool provisioning_reported = false;
  std::string lifecycle_detail;
  NetworkServicePollState network;
};
// Internal health/OTA upkeep; application lifecycle maintenance belongs to the
// Device or ProductRuntime poll immediately before this call.
// I/O/USB upkeep remains per call. Diagnostics/OTA/full boot health follow the
// existing 20 ms upkeep; every false application observation is retained.
void poll_runtime_health(DeviceContext &, bool application_ready = true,
                         bool poll_usb = true, bool sample_health = true);
// Shared Main-only service coordination; each host retains its existing cadence.
// Never pumps a foreign executor or starts a second I/O owner.
void poll_service_lifecycle(DeviceContext &, std::string &last_detail);
void poll_maintenance_runtime(DeviceContext &device, MaintenanceState &state,
                              bool application_ready = true,bool poll_usb = true);
}
