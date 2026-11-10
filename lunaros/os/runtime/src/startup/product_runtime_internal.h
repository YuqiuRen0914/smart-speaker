#pragma once
#include "runtime/device_context.h"
#include "device_api/device_projection.h"
#include "runtime/product_runtime.h"
#include "app/lifecycle/app_controller.h"
#include "maintenance/update_maintenance.h"
#include <new>
namespace lunaredu::lunaros {
// Lifecycle mutation belongs to product composition, never to protocol consumers.
class ProductRuntimeAccess {
  static RuntimeDeviceState &state(const DeviceContext &device) {
    if (!device.runtime_state_) std::abort();
    return *device.runtime_state_;
  }
public:
  static RuntimeUpdateMaintenance *update_maintenance(const DeviceContext &device){return state(device).update_.get();}
  static bool configure_update(DeviceContext &device,UpdateMaintenanceDefinition definition,std::string &error){
    auto &owner=state(device).update_;
    if(!owner){owner.reset(new(std::nothrow) RuntimeUpdateMaintenance(device));if(!owner){error="update_maintenance_memory_unavailable";return false;}}
    return owner->configure(definition,error);
  }
  static bool configure_health(DeviceContext &device,std::span<const RuntimeHealthProbe> health,std::string &error){
    for(const auto &probe:health)if(!probe.id||!*probe.id||!probe.ready){error="runtime_health_probe_invalid";return false;}
    state(device).health_=health;return true;
  }
  static bool product_health_ready(const DeviceContext &device){
    for(const auto &probe:state(device).health_)if(!probe.ready(probe.context))return false;
    return true;
  }
  enum class Phase : uint8_t { Dormant, Bootstrapping, Running, Retiring, Failed };
  static Phase phase(const DeviceContext &device) {
    return static_cast<Phase>(state(device).runtime_phase_.load(std::memory_order_acquire));
  }
  static void publish_phase(DeviceContext &device, Phase phase) {
    state(device).runtime_phase_.store(static_cast<uint8_t>(phase), std::memory_order_release);
  }
  static bool bootstrap(DeviceContext &, KernelRuntime &, apps::ApplicationsService &, DeviceProjectionRegistry &,
      const ServiceExecutorDefinition *,size_t, const NetworkPolicy &,
      const char *, ProductRuntimeHooks, RuntimeBootstrapStage *, std::string &);
  static bool bootstrap(DeviceContext &device,KernelRuntime &runtime,apps::ApplicationsService &applications,
      DeviceProjectionRegistry &projections,std::initializer_list<ServiceExecutorDefinition> executors,
      const NetworkPolicy &network,const char *timezone,ProductRuntimeHooks hooks,RuntimeBootstrapStage *stage,std::string &error) {
    return bootstrap(device,runtime,applications,projections,executors.begin(),executors.size(),network,timezone,hooks,stage,error);
  }
  static bool bind_application_maintenance(DeviceContext &device, apps::ApplicationMaintenancePorts ports) {
    if (state(device).maintenance_.context && state(device).maintenance_.context != ports.context) return false;
    state(device).maintenance_ = ports; return true;
  }
  static void clear_application_maintenance(DeviceContext &device,const void *owner) {
    if (state(device).maintenance_.context==owner) state(device).maintenance_={};
  }
  static void begin_boot_metrics(DeviceContext &device) {
    state(device).bootstrap_available_.store(false, std::memory_order_release);
    state(device).bootstrap_completed_.store(false, std::memory_order_relaxed);
    state(device).bootstrap_succeeded_.store(false, std::memory_order_relaxed);
    state(device).bootstrap_total_ms_.store(0, std::memory_order_relaxed);
    for(auto &value:state(device).bootstrap_stage_ms_)value.store(0,std::memory_order_relaxed);
    state(device).bootstrap_available_.store(true,std::memory_order_release);
  }
  static void record_boot_stage(DeviceContext &device, size_t stage, uint32_t elapsed_ms) {
    if(stage<state(device).bootstrap_stage_ms_.size())state(device).bootstrap_stage_ms_[stage].store(elapsed_ms,std::memory_order_relaxed);
  }
  static void finish_boot_metrics(DeviceContext &device, uint32_t elapsed_ms, bool succeeded) {
    state(device).bootstrap_total_ms_.store(elapsed_ms,std::memory_order_relaxed);
    state(device).bootstrap_succeeded_.store(succeeded,std::memory_order_relaxed);
    state(device).bootstrap_completed_.store(true,std::memory_order_release);
  }
  static void configure_boot_health(DeviceContext &device, const RuntimePreset &preset) {
    state(device).health_requirements_ = preset.health;
    state(device).observe_health_ = preset.observe_health;
  }
  static uint32_t additional_health_blocks(DeviceContext &device) {
    const auto requirements = state(device).health_requirements_;
    if(!requirements.reachability&&!requirements.platform&&!requirements.device_api)return 0;
    const auto observation = state(device).observe_health_ ? state(device).observe_health_(device) : RuntimeHealthObservation{};
    return (requirements.reachability && !observation.reachability ? Reachability : 0U) |
        (requirements.platform && !observation.platform ? Platform : 0U) |
        (requirements.device_api && !observation.device_api ? DeviceApi : 0U);
  }
  static void reset_boot_health(DeviceContext &device, uint32_t period_ms = 3000) {
    state(device).boot_healthy_period_ms_ = period_ms;
    state(device).boot_health_tracking_ = false;
    state(device).boot_started_.store(false, std::memory_order_relaxed);
    state(device).boot_health_observation_.store(0, std::memory_order_relaxed);
  }
  static void boot_started(DeviceContext &device) { state(device).boot_started_.store(true, std::memory_order_release); }
  static constexpr uint32_t Application = BootHealthObservation::Application;
  static constexpr uint32_t Io = BootHealthObservation::Io;
  static constexpr uint32_t RequiredServices = BootHealthObservation::RequiredServices;
  static constexpr uint32_t Reachability = BootHealthObservation::Reachability;
  static constexpr uint32_t Platform = BootHealthObservation::Platform;
  static constexpr uint32_t DeviceApi = BootHealthObservation::DeviceApi;
  static constexpr uint32_t Bootstrap = BootHealthObservation::Bootstrap;
  static constexpr uint32_t Observed = BootHealthObservation::Observed;
  static void observe_boot_health(DeviceContext &device, uint32_t blocks) {
    if (!state(device).boot_started_.load(std::memory_order_acquire)) blocks |= Bootstrap;
    state(device).boot_health_observation_.store(blocks | Observed, std::memory_order_release);
  }
  // A false application observation must survive recovery between upkeep polls.
  // Main owns the timer; retain other last-observed blockers until the next sample.
  static void interrupt_application_health(DeviceContext &device) {
    state(device).boot_health_tracking_ = false;
    uint32_t blocks = Application | Observed;
    if (!state(device).boot_started_.load(std::memory_order_acquire)) blocks |= Bootstrap;
    state(device).boot_health_observation_.fetch_or(blocks, std::memory_order_release);
  }
  static bool boot_healthy(DeviceContext &device, uint32_t now, bool healthy) {
    if (!state(device).boot_started_.load(std::memory_order_acquire) || !healthy) { state(device).boot_health_tracking_ = false; return false; }
    if (!state(device).boot_health_tracking_) {
      state(device).boot_healthy_since_ms_ = now;
      state(device).boot_health_tracking_ = true;
    }
    return uint32_t(now - state(device).boot_healthy_since_ms_) >= state(device).boot_healthy_period_ms_;
  }
  static bool reset(DeviceProjectionRegistry &registry, std::string &error) { return registry.reset(error); }
  static bool seal_app(DeviceContext &device, std::string &error) {
    return !device.application() || device.application()->seal(error);
  }
  static bool reset_app(DeviceContext &device, std::string &error) {
    return !device.application() || device.application()->reset_composition(error);
  }
  static bool seal(DeviceProjectionRegistry &registry, std::string *error) { return registry.seal(error); }
};
}
