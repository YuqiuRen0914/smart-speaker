#pragma once
#include <atomic>
#include <memory>
#include "runtime/preset.h"
#include "runtime/device_context.h"
#include "app/execution/application_maintenance.h"
#include "device_api/adapters/application_projection.h"
#include "device_api/device_frame_endpoint.h"

namespace lunaredu::lunaros {
// Runtime owns boot mutation; Device Context exposes only borrowed observation callbacks.
class RuntimeUpdateMaintenance;
class RuntimeDeviceState {
public:
  explicit RuntimeDeviceState(DeviceContext &);
  ~RuntimeDeviceState();
  RuntimeDeviceState(const RuntimeDeviceState &) = delete;
  RuntimeDeviceState &operator=(const RuntimeDeviceState &) = delete;
private:
  friend class ProductRuntimeAccess;
  DeviceContext &device_;
  std::unique_ptr<RuntimeUpdateMaintenance> update_;
  apps::ApplicationMaintenancePorts maintenance_;
  ApplicationMaintenanceBinding maintenance_binding_;
  ApplicationProtocolBinding application_ports_;
  DeviceFrameOwner frame_owner_;
  std::span<const RuntimeHealthProbe> health_{};
  uint32_t boot_healthy_period_ms_ = 3000, boot_healthy_since_ms_ = 0;
  bool boot_health_tracking_ = false;
  BootHealthRequirements health_requirements_{};
  RuntimeHealthObservation (*observe_health_)(DeviceContext &) = nullptr;
  std::atomic<uint8_t> runtime_phase_{0};
  std::atomic<bool> boot_started_{false};
  std::atomic<uint32_t> boot_health_observation_{0};
  std::atomic<bool> bootstrap_available_{false}, bootstrap_completed_{false}, bootstrap_succeeded_{false};
  std::atomic<uint32_t> bootstrap_total_ms_{0};
  std::array<std::atomic<uint32_t>, kBootstrapStageNames.size()> bootstrap_stage_ms_{};
  DeviceApiObservationBinding observation_;
};
}
