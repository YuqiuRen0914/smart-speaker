#include "lunaros_platform_link_service_id.h"
#include "lunaros_network_service_id.h"
#ifdef LUNAROS_PROVISIONING_COMPONENT
#include "lunaros_provisioning.h"
#endif
#include "lunaros_network.h"
#include "maintenance/maintenance.h"
#include "startup/product_runtime_internal.h"
#include "runtime/maintenance_timing.h"
#include "diagnostics/cpu_monitor.h"
#include "diagnostics/elapsed_work_report.h"
#include "runtime/product_runtime.h"
#include "lunaros_network_lifecycle.h"
#include "kernel/runtime.h"
#include "lunaros_platform_link.h"
#include "lunaros_network_espnow.h"
#include "lunaros_local_web_lifecycle.h"
#include "lunaros_resource_managed_assets.h"
#include "lunaros_resource_service_id.h"
#include <cstdio>
#include <esp_timer.h>
#include <esp_log.h>

namespace lunaredu::lunaros {
void poll_product_radio(DeviceContext &device) {
  if(device.runtime.current_task_owns_executor(kMainServiceExecutor) &&
      network_runtime_owned_by(device.runtime))tick_espnow();
}
namespace {
MaintenanceWorkTiming maintenance_timing;
MaintenanceWorkTiming network_timing;
system::ElapsedWorkReporter<size_t(MaintenanceWorkTimingStage::Count)> maintenance_reporter,network_reporter;
template<class Action> decltype(auto) measure(MaintenanceWorkTimingStage stage,Action &&action) {
  return maintenance_timing.measure(size_t(stage),[]{return uint64_t(esp_timer_get_time());},std::forward<Action>(action));
}
uint32_t now_ms() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }
}
MaintenanceWorkTiming::Snapshot read_maintenance_work_timing() {
  maintenance_timing.enable(system::main_work_profiling_active());return maintenance_timing.snapshot();
}
void poll_network_services(DeviceContext &device, NetworkServicePollState &state,
                           uint32_t platform_period_ms) {
  auto &runtime = device.runtime;
  if (!runtime.current_task_owns_executor(kNetworkServiceExecutor) ||
      !network_runtime_owned_by(runtime) ||
      !runtime.service_available(kNetworkRuntimeServiceId)) return;
  poll_network_on_owner();
#ifdef LUNAROS_PROVISIONING_COMPONENT
  if(runtime.service_available(kProvisioningServiceId))poll_provisioning_on_owner();
#endif
  if (!runtime.service_available(kPlatformLinkServiceId)) return;
  const auto now = now_ms();
  if (state.platform_polled && now - state.last_platform_poll_ms < platform_period_ms) return;
  state.platform_polled = true;
  state.last_platform_poll_ms = now;
  network_timing.enable(system::main_work_profiling_active());
  network_timing.measure(size_t(MaintenanceWorkTimingStage::PlatformBoundary),[]{return uint64_t(esp_timer_get_time());},[]{tick_platform_link_runtime();});
  network_reporter.report("runtime.network",network_timing.snapshot(),maintenance_work_stage_names);
}
void poll_service_lifecycle(DeviceContext &device, std::string &last_detail) {
  auto &runtime = device.runtime;
  if (!runtime.current_task_owns_executor(kMainServiceExecutor)) return;
  std::string error;
  switch (ProductRuntimeAccess::phase(device)) {
  case ProductRuntimeAccess::Phase::Running:
    (void)runtime.reconcile_services(error);
    break;
  case ProductRuntimeAccess::Phase::Retiring:
    if (runtime.stop_all_services(error))
      ProductRuntimeAccess::publish_phase(device, ProductRuntimeAccess::Phase::Failed);
    break;
  default:
    return; // No implicit service admission before or after a failed OS bootstrap.
  }
  if (error != last_detail) {
    if (!error.empty()) ESP_LOGW("lunaros.lifecycle", "%s", error.c_str());
    last_detail = error;
  }
}
void poll_maintenance_runtime(DeviceContext &device, MaintenanceState &state, bool application_ready,bool poll_usb) {
  auto &runtime = device.runtime;
  if (!runtime.current_task_owns_executor(kMainServiceExecutor)) return;
  if (!application_ready) ProductRuntimeAccess::interrupt_application_health(device);
  maintenance_timing.enable(system::main_work_profiling_active());
  maintenance_reporter.report("runtime.main",maintenance_timing.snapshot(),maintenance_work_stage_names);
  measure(MaintenanceWorkTimingStage::MainExecutor,[&]{runtime.pump_executor(kMainServiceExecutor, 0);});
  const auto now = now_ms();
  if (state.upkeep_polled && now - state.last_network_poll < kRuntimeUpkeepIntervalMs) return;
  state.upkeep_polled = true;
  state.last_network_poll = now;
  measure(MaintenanceWorkTimingStage::NetworkExecutor,[&]{runtime.pump_executor(kNetworkServiceExecutor, 0);});
  measure(MaintenanceWorkTimingStage::Lifecycle,[&]{poll_service_lifecycle(device, state.lifecycle_detail);});
  if(runtime.service_available(kResourceServiceId))poll_managed_asset_recovery();
  measure(MaintenanceWorkTimingStage::Network,[&]{poll_network_services(device, state.network);});
  measure(MaintenanceWorkTimingStage::Web,[&]{if(network_runtime_owned_by(runtime))poll_local_web();});
#ifdef LUNAROS_PROVISIONING_COMPONENT
  if(network_runtime_owned_by(runtime)) {
  const auto setup=provisioning_fast_snapshot();
  const bool provisioning=setup.state==ProvisioningState::Active;
  if(provisioning&&!state.provisioning_reported){const auto info=provisioning_snapshot();
    printf("LunarOS setup Wi-Fi: %s password: %s URL: http://%s/setup\n",info.ssid.c_str(),provisioning_local_password().c_str(),info.ip.c_str());}
  state.provisioning_reported=provisioning;
  }
#endif
  measure(MaintenanceWorkTimingStage::Product,[&]{poll_runtime_health(device, application_ready,poll_usb);});
}

} // namespace lunaredu::lunaros
