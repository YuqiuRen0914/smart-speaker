#include "diagnostics/function_probe.h"
#include "lunaros_update_service_id.h"
#include "runtime/product_runtime.h"
#include "lunaros_applications.h"
#include "maintenance/maintenance.h"
#include "maintenance/module_maintenance.h"
#include "startup/product_runtime_internal.h"
#include "lunaros_ota_lifecycle.h"
#include "lunaros_usb.h"
#include "diagnostics/runtime_diagnostics.h"
#include "diagnostics/cpu_monitor.h"
#include "diagnostics/elapsed_work_report.h"
#include "sdkconfig.h"
#include "system/boot_guard.h"
#include <esp_timer.h>

namespace lunaredu::lunaros {
namespace {
enum class ProductWorkStage : size_t {Applications,Lifecycle,Diagnostics,MaintenanceIo,Usb,Health,Ota,Count};
constexpr std::array<const char *,size_t(ProductWorkStage::Count)> product_work_names{
    "applications","lifecycle","diagnostics","maintenance_io","usb","health","ota"};
system::ElapsedWorkTiming<size_t(ProductWorkStage::Count)> product_work_timing;
system::ElapsedWorkReporter<size_t(ProductWorkStage::Count)> product_work_reporter;
template<class Action> decltype(auto) measure_product(ProductWorkStage stage,Action &&action) {
  system::probe::Scope diagnostic_scope(static_cast<system::probe::Point>(unsigned(system::probe::Point::RuntimeApplications)+unsigned(stage)));
  return product_work_timing.measure(size_t(stage),[]{return uint64_t(esp_timer_get_time());},std::forward<Action>(action));
}
}
void ProductRuntime::poll(bool application_ready) {
  if (!runtime_.current_task_owns_executor(kMainServiceExecutor)) return;
  product_work_timing.enable(system::main_work_profiling_active());
  measure_product(ProductWorkStage::Applications,[&]{applications_.poll();});
  const auto now = static_cast<uint32_t>(esp_timer_get_time() / 1000);
  const bool upkeep_due = !upkeep_polled_ || now - last_upkeep_poll_ms_ >= kRuntimeUpkeepIntervalMs;
  if (upkeep_due) {
    upkeep_polled_ = true;
    last_upkeep_poll_ms_ = now;
    measure_product(ProductWorkStage::Lifecycle,[&]{poll_service_lifecycle(device_, lifecycle_detail_);});
  }
  poll_runtime_health(device_, application_ready, true, upkeep_due);
  poll_modules();
}
void ProductRuntime::poll_modules() {
  if (module_maintenance_ && ProductRuntimeAccess::phase(device_)==ProductRuntimeAccess::Phase::Running)
    module_maintenance_->poll(runtime_,static_cast<uint32_t>(esp_timer_get_time()/1000));
}
void poll_runtime_health(DeviceContext &device, bool application_ready, bool poll_usb, bool sample_health) {
  auto &runtime = device.runtime;
  if (!runtime.current_task_owns_executor(kMainServiceExecutor)) return;
  product_work_timing.enable(system::main_work_profiling_active());
  product_work_reporter.report("runtime.health",product_work_timing.snapshot(),product_work_names);
#if CONFIG_LUNAROS_BOOT_GUARD
  system_boot_guard_heartbeat(BootGuardOwner::Main);
#endif
  if (!application_ready) ProductRuntimeAccess::interrupt_application_health(device);
  if(sample_health)
    measure_product(ProductWorkStage::Diagnostics,[&]{system::poll_runtime_diagnostics(runtime);});
  measure_product(ProductWorkStage::MaintenanceIo,[&]{poll_maintenance_io(runtime);});
  if(poll_usb && maintenance_io_owned_by(runtime))
    if(auto *projections=device.projections())measure_product(ProductWorkStage::Usb,[&]{poll_usb_device_api(*projections);});
  const auto now = static_cast<uint32_t>(esp_timer_get_time() / 1000);
  using Health = ProductRuntimeAccess;
  uint32_t blocks = 0;
  const auto sample = [&] {
    // The same bounded probe contract applies to Device and dedicated task
    // hosts, and runs only at their existing health sampling cadence.
    const bool declared_ready=Health::product_health_ready(device);
    return (application_ready && declared_ready ? 0U : Health::Application) |
        (product_runtime_io_healthy(runtime) ? 0U : Health::Io) |
        (runtime.required_services_ready() ? 0U : Health::RequiredServices) |
        Health::additional_health_blocks(device);
  };
  if (sample_health) {
    blocks = measure_product(ProductWorkStage::Health,sample);
    Health::observe_boot_health(device, blocks);
#if CONFIG_LUNAROS_BOOT_GUARD
    const bool update_ready = runtime.service_available(kUpdateServiceId);
    system_boot_guard_observe_health(blocks == 0 && update_ready, blocks | (update_ready ? 0U : 256U));
    struct HealthWindow { uint32_t start=0, samples=0, failed=0, mask=0, update_missing=0; };
    static HealthWindow health_window;
    if (system::cpu_monitor_active()) {
      if (!health_window.samples) health_window.start=now;
      ++health_window.samples;
      health_window.failed+=blocks!=0;
      health_window.mask|=blocks;
      health_window.update_missing+=!runtime.service_available(kUpdateServiceId);
      if (now-health_window.start>=2000) {
        ESP_LOGI("runtime.health", "HEALTH_WINDOW samples=%u failed=%u blocked_mask=%u update_missing=%u window_ms=%u",
            unsigned(health_window.samples),unsigned(health_window.failed),unsigned(health_window.mask),
            unsigned(health_window.update_missing),unsigned(now-health_window.start));
        health_window={};
      }
    } else health_window={};
#endif
  }
  if (!runtime.service_available(kUpdateServiceId)) {
    // Unavailable update ownership cannot count toward continuous confirmation.
    // Recovery starts a fresh window, even when sampling was throttled.
    (void)ProductRuntimeAccess::boot_healthy(device, now, false);
    return;
  }
  // OTA admission/retirement is handled by the executor checkpoint. Its
  // deadlines and diagnostics need the existing 20 ms upkeep, not duplicate
  // work on every faster product release. USB and I/O wakes stay per-call.
  if (!sample_health) return;
  measure_product(ProductWorkStage::Ota,[]{tick_ota_service();});
  if (!ota_service_boot_pending()) {
    (void)ProductRuntimeAccess::boot_healthy(device, now, false);
    return;
  }
  const bool healthy = blocks == 0;
  if (ProductRuntimeAccess::boot_healthy(device, now, healthy))
    (void)ota_service_confirm_running_image();
}

} // namespace lunaredu::lunaros
