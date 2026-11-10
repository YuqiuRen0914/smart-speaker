#include "lunaros_network_policy.h"
#ifdef LUNAROS_PROVISIONING_COMPONENT
#include "lunaros_provisioning.h"
#endif
#include "runtime/product_runtime.h"
#include "startup/product_runtime_internal.h"
#include "maintenance/maintenance.h"
#include "maintenance/module_maintenance.h"
#include "presets/backends.h"
#include "kernel/service_executor.h"
#include "configuration/lifecycle.h"
#include "runtime/configuration_bindings.h"
#include "lunaros_network_lifecycle.h"
#include "lunaros_network_jobs.h"
#include "lunaredu_ota.h"
#include "lunaros_resource_managed_assets.h"
#include <esp_timer.h>
#include "sdkconfig.h"
#ifdef ESP_PLATFORM
#include "diagnostics/memory_pressure.h"
#endif

namespace lunaredu::lunaros {
namespace {
bool firmware_update_blocks_assets() {
  const auto status=ota_service_status_snapshot();
  return !status.boot_confirmed || status.pending_verify || status.upload_active ||
      status.pull_queued || status.pull_active || status.restart_pending ||
      network_maintenance_state()!=NetworkMaintenanceState::Normal;
}
}
ProductRuntime::ProductRuntime() : device_(runtime_), state_(device_), projections_(device_) {}
ProductRuntime::~ProductRuntime() { assert_owner_lifetime(); }
bool ProductRuntime::admit_start(std::string &error) {
  if (attempted_) { error="product_runtime_start_already_attempted"; return false; }
  attempted_=true; error.clear(); return true;
}
bool ProductRuntime::start(std::initializer_list<ServiceExecutorDefinition> executors,
    const NetworkPolicy &network, const char *timezone, ProductRuntimeHooks hooks,
    RuntimeBootstrapStage *failed_stage, std::string &error) {
  if (!admit_start(error)) return false;
  struct Boot { ProductRuntime &owner; ProductRuntimeHooks hooks; } boot{*this,hooks};
  hooks.context=&boot;
  hooks.register_services=[](void *opaque,std::string &failure) {
    auto &boot=*static_cast<Boot *>(opaque);
    if(!boot.hooks.register_services){failure="runtime_service_registration_hook_missing";return false;}
    if (!boot.hooks.register_services(boot.hooks.context,failure)) return false;
    if (!boot.hooks.module_maintenance.empty()) {
      auto maintenance=std::make_unique<ModuleMaintenanceHost>();
      if (!maintenance->configure(boot.hooks.module_maintenance.data(),boot.hooks.module_maintenance.size(),
          boot.owner.runtime_,failure)) return false;
      boot.owner.module_maintenance_=std::move(maintenance);
    }
    return true;
  };
  hooks.start_executor_tasks=[](void *opaque,std::string &failure) {
    auto &boot=*static_cast<Boot *>(opaque);
    if(!boot.hooks.start_executor_tasks){failure="runtime_executor_task_hook_missing";return false;}
    // A timed-out or partially admitted task may still borrow the owner.
    boot.owner.worker_admitted_=true;
    return boot.hooks.start_executor_tasks(boot.hooks.context,failure);
  };
  return ProductRuntimeAccess::bootstrap(device_,runtime_,applications_,projections_,executors,network,
      timezone,hooks,failed_stage,error);
}
// One ordered OS startup path, shared by Device and advanced ProductRuntime.
// Product hooks contribute their graph/tasks; ServiceHost owns dependency order.
bool ProductRuntimeAccess::bootstrap(
    DeviceContext &device, KernelRuntime &runtime, apps::ApplicationsService &applications, DeviceProjectionRegistry &projections,
    const ServiceExecutorDefinition *executors,size_t executor_count,
    const NetworkPolicy &network_policy, const char *timezone,
    ProductRuntimeHooks hooks, RuntimeBootstrapStage *failed_stage,
    std::string &error) {
  error.clear();
  set_managed_asset_update_busy_probe(firmware_update_blocks_assets);
  // The watchdog/rescue mechanism must cover configuration failures too.
#if CONFIG_LUNAROS_BOOT_GUARD
  ota_service_start_boot_guard();
#endif
  RuntimeBootstrapStage stage = RuntimeBootstrapStage::CoreRuntime;
  DeviceContext *metrics_device = nullptr;
  Phase failure_phase = Phase::Failed;
  const int64_t began = esp_timer_get_time();
  int64_t phase_began = began;
  const auto observe = [&](const char *state,const char *reason) {
    if (hooks.startup_observer)
      hooks.startup_observer(hooks.startup_observer_context,runtime_bootstrap_stage_name(stage),state,reason);
  };
  observe("enter","");
  const auto record_stage = [&] {
#ifdef ESP_PLATFORM
    system::record_boot_memory_checkpoint(runtime_bootstrap_stage_name(stage));
#endif
    if (metrics_device) ProductRuntimeAccess::record_boot_stage(*metrics_device,
        static_cast<size_t>(stage), static_cast<uint32_t>((esp_timer_get_time() - phase_began) / 1000));
  };
  const auto next_stage = [&](RuntimeBootstrapStage next) {
    observe("ready","");
    record_stage();
    stage = next;
    phase_began = esp_timer_get_time();
    observe("enter","");
  };

  const auto fail = [&](RuntimeBootstrapStage failed) {
    // A rejected internal owner may refine the phase before any side effects.
    stage = failed;
    observe("failed",error.c_str());
    record_stage();
    if (metrics_device) ProductRuntimeAccess::finish_boot_metrics(*metrics_device,
        static_cast<uint32_t>((esp_timer_get_time() - began) / 1000), false);
    // No task/serving has been admitted in these stages; roll back declarations.
    if (&projections.runtime() == &runtime && &projections.device() == static_cast<DeviceApiContext *>(&device) &&
        projections.owns_device() && (failed == RuntimeBootstrapStage::ServiceRegistration ||
                             failed == RuntimeBootstrapStage::DeviceProjections)) {
      std::string reset_error;
      if (!ProductRuntimeAccess::reset(projections, reset_error) ||
          !ProductRuntimeAccess::reset_app(device, reset_error))
        error += "; reset: " + reset_error;
    }
    if (failed_stage != nullptr) {
      *failed_stage = failed;
    }
    if (metrics_device) publish_phase(*metrics_device, failure_phase);
    if (metrics_device && failed < RuntimeBootstrapStage::ExecutorTasks)
      state(*metrics_device).frame_owner_.release();
    return false;
  };

  if (&projections.runtime() != &runtime || &device.runtime != &runtime ||
      &projections.device() != static_cast<DeviceApiContext *>(&device) || !projections.owns_device()) {
    error = "runtime_projection_owner_mismatch";
    return fail(RuntimeBootstrapStage::DeviceProjections);
  }
  const auto &preset = selected_runtime_preset(hooks.preset);
  if (!validate_runtime_preset(preset,error)) return fail(RuntimeBootstrapStage::CoreRuntime);
  if (!state(device).frame_owner_.bind()) {
    error = "runtime_frame_owner_conflict"; return fail(RuntimeBootstrapStage::CoreRuntime);
  }
  if(!configure_health(device,hooks.health,error))return fail(stage);
  configure_boot_health(device,preset);
  metrics_device = &device;
  publish_phase(*metrics_device, Phase::Bootstrapping);
  ProductRuntimeAccess::begin_boot_metrics(*metrics_device);
#ifdef ESP_PLATFORM
  system::record_boot_memory_checkpoint("before_core");
#endif
  // 1. Core execution environment and lazy application backend factories.
  if (!setup_core_runtime(runtime, executors,executor_count,error)) {
    return fail(stage);
  }
  if (preset.configure_backends && !preset.configure_backends(error)) return fail(stage);
  if (!configure_execution_backends(preset,error)) return fail(stage);
  if (!runtime.enforce_task_ownership(error)) return fail(stage);
  // 2. Persistent system configuration and boot health policy.
  next_stage(RuntimeBootstrapStage::Configuration);
  if (!bind_system_configuration(error) || !initialize_configuration(error)) return fail(stage);
  ProductRuntimeAccess::reset_boot_health(device, hooks.boot_healthy_period_ms);
  if (hooks.bind_main_and_network_to_caller &&
      (!runtime.bind_executor(kMainServiceExecutor, &error) ||
       !runtime.bind_executor(kNetworkServiceExecutor, &error))) return fail(stage);
  // 3. Bind network policy without activating the product App.
  next_stage(RuntimeBootstrapStage::Network);
  if (timezone == nullptr || timezone[0] == '\0' ||
      !configure_network_runtime(runtime, network_policy, timezone, error)) {
    if (timezone == nullptr || timezone[0] == '\0') {
      error = "runtime_timezone_missing";
    }
    return fail(stage);
  }
#ifdef LUNAROS_PROVISIONING_COMPONENT
  auto provisioning=hooks.provisioning ? *hooks.provisioning : lunaredu::lunaros::ProvisioningPolicy{};
  if(!network_policy.station_enabled)provisioning.enabled=false;
  if(!configure_provisioning_runtime(runtime,provisioning,error))return fail(stage);
#else
  if(hooks.provisioning) {
    error="provisioning_component_not_selected";
    return fail(stage);
  }
#endif
  if (!ProductRuntimeAccess::reset(projections, error)) return fail(stage);
  if (!ProductRuntimeAccess::reset_app(device, error)) return fail(stage);
  // 4. Install the complete service graph and non-running declarations.
  next_stage(RuntimeBootstrapStage::ServiceRegistration);
  if (hooks.register_services == nullptr ||
      !hooks.register_services(hooks.context, error)) {
    if (hooks.register_services == nullptr) {
      error = "runtime_service_registration_hook_missing";
    }
    return fail(stage);
  }
  // 5. Seal external interfaces before any worker can serve requests.
  next_stage(RuntimeBootstrapStage::DeviceProjections);
  if (!projections.context().control.seal(error) ||
      !ProductRuntimeAccess::seal_app(device, error) ||
      !ProductRuntimeAccess::seal(projections, &error)) return fail(stage);
  // 6. Admit executor owners and shared maintenance I/O.
  next_stage(RuntimeBootstrapStage::ExecutorTasks);
  if (hooks.start_executor_tasks == nullptr ||
      !hooks.start_executor_tasks(hooks.context, error)) {
    if (hooks.start_executor_tasks == nullptr) {
      error = "runtime_executor_task_hook_missing";
    }
    return fail(stage);
  }
  if (!start_maintenance_io(runtime, applications, error)) return fail(stage);
  // 7. Start according to dependencies; deferred readiness continues in poll.
  next_stage(RuntimeBootstrapStage::Services);
  if (!runtime.start_all_services(error)) {
    const std::string startup_error = error;
    std::string cleanup_error;
    // Main still sees Bootstrapping: only this caller owns the initial rollback.
    // Transfer pending cleanup after stop_all returns; never run two stop passes.
    if (!runtime.stop_all_services(cleanup_error)) {
      failure_phase = Phase::Retiring;
      error = startup_error + ";service_cleanup_failed:" + cleanup_error;
    }
    return fail(stage);
  }
  record_stage();
  observe("ready","");
  ProductRuntimeAccess::finish_boot_metrics(*metrics_device,
      static_cast<uint32_t>((esp_timer_get_time() - began) / 1000), true);
  ProductRuntimeAccess::boot_started(device);
  publish_phase(*metrics_device, Phase::Running);
  return true;
}

const char *runtime_bootstrap_stage_name(RuntimeBootstrapStage stage) {
  switch (stage) {
  case RuntimeBootstrapStage::CoreRuntime:
    return "core_runtime";
  case RuntimeBootstrapStage::Configuration:
    return "configuration";
  case RuntimeBootstrapStage::Network:
    return "network";
  case RuntimeBootstrapStage::ServiceRegistration:
    return "service_registration";
  case RuntimeBootstrapStage::DeviceProjections:
    return "device_projections";
  case RuntimeBootstrapStage::ExecutorTasks:
    return "executor_tasks";
  case RuntimeBootstrapStage::Services:
    return "services";
  }
  return "unknown";
}

const char *runtime_bootstrap_failure_reason(RuntimeBootstrapStage stage) {
  switch (stage) {
  case RuntimeBootstrapStage::CoreRuntime:
    return "lunaros_core_setup_failed";
  case RuntimeBootstrapStage::Configuration:
    return "configuration_initialization_failed";
  case RuntimeBootstrapStage::Network:
    return "network_service_binding_failed";
  case RuntimeBootstrapStage::ServiceRegistration:
    return "service_registry_failed";
  case RuntimeBootstrapStage::DeviceProjections:
    return "device_projection_sealing_failed";
  case RuntimeBootstrapStage::ExecutorTasks:
    return "managed_task_start_failed";
  case RuntimeBootstrapStage::Services:
    return "service_start_failed";
  }
  return "runtime_bootstrap_failed";
}

} // namespace lunaredu::lunaros
