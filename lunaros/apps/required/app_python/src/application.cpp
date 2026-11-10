#include "app/execution/application_context.h"
#include "device_api/adapters/application_projection.h"
#include "app/lifecycle/app_controller_view.h"
#include "lunaros_python_application.h"
#include "lunaros_python_view.h"
#include "app/lifecycle/app_controller.h"
#include "device_api/device_api_context.h"
#include "lunaros_micropython_runtime_config.h"
#include "lunaros_micropython_runtime.h"
#include <esp_log.h>
namespace lunaredu::python {
Presentation presentation() {
  const auto status=micropython_run_flags();
  return {status.running,status.stop_requested};
}
SystemApplication::SystemApplication(app::AppController &applications, lunaros::ApplicationContext &device)
    : applications_(applications), device_(device) {}
bool SystemApplication::declare(std::string &error) {
  return applications_.register_foreground(definition(), error);
}
app::ForegroundDefinition SystemApplication::definition() {
  static constexpr app::ApplicationServiceRequirement services[] = {{"runtime.micropython", true}};
  static constexpr lunaros::ResourceRequirement resources[] = {
      {"python.vm", lunaros::ResourceAccess::Exclusive},
      {"display.main", lunaros::ResourceAccess::Exclusive}};
  app::ForegroundDefinition result;
  result.id = kApplicationId;
  result.backend = app::ApplicationBackend::MicroPython;
  result.manifest.title = "Python";
  result.manifest.icon = "code";
  result.manifest.services = services; result.manifest.service_count = 1;
  result.manifest.resources = resources; result.manifest.resource_count = 2;
  result.context = this;
  result.prepare = [](void *p, std::string &error) {
    if (micropython_runtime_status().running && !micropython_background_active()) { error = "python_runtime_busy"; return false; }
    auto &self = *static_cast<SystemApplication *>(p);
    self.expected_background_execution_=micropython_background_active()?micropython_runtime_status().execution_id:0;
    if (!self.temporary_) {
      const char *previous = lunaredu::app::AppControllerView{self.applications_}.active_application_id();
      self.return_to_ = previous ? previous : "";
    }
    return true;
  };
  result.start = [](void *p, lunaredu::app::ApplicationEnterReason reason, std::string &error) {
    auto &self = *static_cast<SystemApplication *>(p);
    if (!self.start(error)) return app::ApplicationStartResult::Failed;
    self.boot_autorun_pending_ = reason == app::ApplicationEnterReason::Boot;
    return app::ApplicationStartResult::Started;
  };
  result.stop_checked = [](void *p, const char *reason, std::string &error) {
    return static_cast<SystemApplication *>(p)->stop(reason, error);
  };
  result.tick = [](void *p) { static_cast<SystemApplication *>(p)->tick(); };
  return result;
}
SystemApplication *SystemApplication::from(const lunaros::DeviceApiContext &device) {
  return static_cast<SystemApplication *>(device_api_application_foreground(device, kApplicationId));
}
bool SystemApplication::foreground(const lunaros::DeviceApiContext &device) {
  auto *self = from(device);
  return self && self->foreground_.load(std::memory_order_acquire);
}
bool SystemApplication::start(std::string &error) {
  // The page owns a ready VM until checked exit. Startup itself runs on the
  // existing transient preparation owner, never waiting on the UI loop.
  // A product callback can fail after partially suspending hardware. Preserve
  // its cleanup obligation even though foreground_ has not been committed.
  transition_attempted_ = true;
  if (!micropython::transition_foreground(true, error)) return false;
  const auto current=micropython_runtime_status();
  if(current.initialized&&micropython_background_active()){
    if(expected_background_execution_&&current.execution_id!=expected_background_execution_){error="python_background_execution_changed";return false;}
    if(!micropython_attach_background_foreground(current.execution_id,error))return false;
    foreground_.store(true,std::memory_order_release);vm_entry_pending_=false;return true;
  }
  if(expected_background_execution_){error="python_background_execution_retired";return false;}
  if (!micropython_prepare_foreground(error)) return false;
  vm_entry_pending_ = true;
  if (!micropython::prepare_runtime_async(error)) return false;
  foreground_.store(true, std::memory_order_release);
  return true;
}
bool SystemApplication::stop(const char *reason, std::string &error) {
  if (!transition_attempted_) return true;
  // Completion is not quiescence: entered/REPL state, queued commands, the VM
  // heap and the managed task must retire before releasing the foreground lease.
  // A rejected shutdown keeps the product suspended and permits an exit retry.
  (void)reason;
  if (vm_entry_pending_ || foreground_.load(std::memory_order_acquire)) {
    if (micropython::runtime_preparation_pending()) {
      error = "python_runtime_start_pending";
      return false;
    }
    if (!micropython_shutdown_runtime(device_.runtime, error)) return false;
  }
  if (!micropython::transition_foreground(false, error)) return false;
  transition_attempted_ = false;
  foreground_.store(false, std::memory_order_release);
  accepted_ = false; temporary_ = false; repl_ = false;
  boot_autorun_pending_ = false;
  vm_entry_pending_ = false;
  return true;
}
bool SystemApplication::enter(lunaros::DeviceApiContext &device, std::string &error) {
  auto *self = from(device);
  if (!self) { error = "python_system_application_unavailable"; return false; }
  if (self->foreground_.load(std::memory_order_acquire)) {
    if (micropython_runtime_status().running) { error = "python_runtime_busy"; return false; }
    return true;
  }
  const char *previous = lunaredu::app::AppControllerView{self->applications_}.active_application_id();
  self->return_to_ = previous ? previous : "";
  self->temporary_ = true; self->accepted_ = false;
  if (!self->applications_.switch_to(kApplicationId, error)) {
    self->temporary_ = false; return false;
  }
  return self->foreground_.load(std::memory_order_acquire);
}
void SystemApplication::accepted(lunaros::DeviceApiContext &device, bool repl) {
  if (auto *self = from(device)) { self->accepted_ = true; self->repl_ = repl; }
}
bool SystemApplication::leave(lunaros::DeviceApiContext &device, std::string &error) {
  auto *self = from(device);
  if (!self) { error = "python_system_application_unavailable"; return false; }
  return self->restore(error);
}
bool SystemApplication::leave_foreground(lunaros::ApplicationContext &device, std::string &error) {
  auto *controller = device.application();
  auto *application = controller ? static_cast<SystemApplication *>(controller->foreground_context(kApplicationId)) : nullptr;
  if (!application) { error = "python_system_application_unavailable"; return false; }
  return application->restore(error);
}
void SystemApplication::rejected(lunaros::DeviceApiContext &device) {
  if (auto *self = from(device); self && self->temporary_) {
    std::string error;
    if (!self->restore(error)) ESP_LOGE("python.app", "failed to restore after rejection: %s", error.c_str());
  }
}
bool SystemApplication::restore(std::string &error) {
  // User switching to another application must never be overwritten by a late result.
  const char *active = lunaredu::app::AppControllerView{applications_}.active_application_id();
  if (!active || std::string(active) != kApplicationId) return true;
  const std::string target = return_to_;
  if (target.empty()) return applications_.stop("python_complete");
  return applications_.switch_to(target, error);
}
void SystemApplication::tick() {
  if (vm_entry_pending_) {
    const auto status = micropython_runtime_status();
    if (status.initialized) {
      std::string error;
      if (!status.entered && !micropython_runtime_enter(error)) {
        ESP_LOGE("python.app", "VM entry failed: %s", error.c_str());
      }
      vm_entry_pending_ = false;
    } else if (!micropython::runtime_preparation_pending()) {
      // Preserve the actual startup error; a later explicit run/REPL can retry.
      ESP_LOGE("python.app", "VM preparation failed: %s", status.last_error.c_str());
      vm_entry_pending_ = false;
    } else {
      return;
    }
  }
  if (boot_autorun_pending_) {
    boot_autorun_pending_ = false;
    std::string error;
    if (!run_boot_script(device_,error))
      ESP_LOGE("python.app", "boot script failed: %s", error.c_str());
  }
  if (!temporary_ || !accepted_) return;
  const auto status = micropython_runtime_status();
  if (status.running || (repl_ && status.repl_active)) return;
  std::string error;
  if (!restore(error)) ESP_LOGE("python.app", "foreground restore failed: %s", error.c_str());
}
}
