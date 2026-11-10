#include "lunaros_update_service_id.h"
#include "device_api/device_operation_builders.h"
#include "runtime/device_context.h"
#include "model/device_model.h"
#include "runtime/device.h"
#include "runtime/update_maintenance.h"
#include "system/notifications.h"
#include "lunaros_network.h"
#include "lunaredu_local_web_portal.h"
#include "lunaros_resource_managed_assets.h"

#include <cstring>
#include <algorithm>
#include <cstdlib>
#include <vector>
#include <memory>
#include <new>
#include <sdkconfig.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "maintenance/maintenance.h"
#include "maintenance/module_maintenance.h"
#include "composition/product_composition.h"
#include "presets/default.h"
#include "runtime/product_composition.h"
#include "runtime/application_host.h"
#include "runtime/product_runtime.h"
#include "startup/product_runtime_internal.h"
#include "device_api/device_projection.h"
#include "kernel/runtime.h"
#include "lunaredu_ota.h"
#include "lunaros_usb.h"
#include "app/lifecycle/app_controller.h"

namespace lunaredu::lunaros {
struct Device::Impl {
  KernelRuntime runtime;
  DeviceContext context{runtime};
  RuntimeDeviceState state{context};
  DeviceProjectionRegistry projections{context};
  apps::ApplicationsService installed_applications;
  MaintenanceState maintenance;
  ModuleMaintenanceHost module_maintenance;
  bool attempted = false;
  bool worker_admitted = false;
  bool ready = false;
  bool foreground_io_checkpoints = false;
  std::string startup_error = "device_starting";
  struct Control {
    Impl *owner = nullptr;
    const BooleanControl *definition = nullptr;
    DeviceJsonBinding read;
    DeviceJsonBinding write;
    DeviceOperationDiagnosticBinding diagnostic;
    DeviceOperationDefinition operations[2];
    DeviceCapabilityDescriptor capability;
    DeviceModuleProjectionMetadata metadata;
    DeviceModuleProjection module;
  };
  std::vector<Control> controls;
  ApplicationHost applications;
  std::vector<std::unique_ptr<RuntimeExecutionOwner>> execution_owners;

  static bool observe_control(void *opaque, DeviceOperationDiagnosticFacts &facts) {
    const auto &definition = *static_cast<Control *>(opaque)->definition;
    facts.signal_count = 1;
    facts.signals[0] = {"digital_output", definition.diagnostic_resource,
        static_cast<int16_t>(definition.diagnostic_gpio), definition.diagnostic_active_low_known,
        definition.diagnostic_active_low};
    facts.parameter_count = 1;
    facts.parameters[0] = {"value", "boolean", true, 0, 1};
    return true;
  }

  static DeviceOperationResult read_control(void *opaque, JsonVariantConst, JsonObject response) {
    auto &control = *static_cast<Control *>(opaque);
    if (!control.owner->ready) return {false, 503, control.owner->startup_error};
    bool value = false;
    std::string error;
    if (!control.definition->read(value, error))
      return {false, 503, error.empty() ? "control_read_failed" : error};
    response["value"] = value;
    return {};
  }
  static DeviceOperationResult write_control(void *opaque, JsonVariantConst request, JsonObject response) {
    auto &control = *static_cast<Control *>(opaque);
    if (!control.owner->ready) return {false, 503, control.owner->startup_error};
    if (!request.is<JsonObjectConst>() || !request["value"].is<bool>())
      return {false, 400, "invalid_boolean_value"};
    std::string error;
    if (!control.definition->write(request["value"].as<bool>(), error))
      return {false, 500, error.empty() ? "control_write_failed" : error};
    // Read the hardware after writing; never publish an optimistic cached value.
    return read_control(opaque, {}, response);
  }
  static bool declare(void *opaque, std::string &error) {
    auto &self = *static_cast<Impl *>(opaque);
    for (auto &control : self.controls) {
      if (!self.projections.register_projection(
          {&control.capability, &control.module, nullptr, true}, &error)) return false;
    }
    return true;
  }

};

Device::Device() : impl_(new Impl) {}
Device::~Device() {
  // Workers/HTTP callbacks borrow this owner. A live Device is not a scoped
  // resource and must never silently free its runtime while those tasks run.
  if (impl_->worker_admitted) std::abort();
}
apps::ApplicationsService &Device::applications_service() { return impl_->installed_applications; }
DeviceContext &Device::context() { return impl_->context; }
KernelRuntime &Device::runtime() { return impl_->runtime; }
DeviceProjectionRegistry &Device::projections() { return impl_->projections; }

bool Device::start(const DeviceOptions &options,std::string &error,StartupFailureOwner failure_owner) {
  auto &self = *impl_;
  error.clear();
  if (self.attempted) { error = "device_start_already_attempted"; return false; }
  self.attempted = true;
  notifications::reset();
  const auto fail = [&] {
    if (error.empty()) error = "device_start_failed";
    self.startup_error = error;
    if (options.startup_observer)
      options.startup_observer(options.startup_observer_context,"device_start","failed",error.c_str());
    if (failure_owner==StartupFailureOwner::Device&&!self.runtime.service_available(kUpdateServiceId))
      (void)ota_service_rollback_running_image(error.c_str());
    return false;
  };
  if (!validate_runtime_preset(selected_runtime_preset(options.preset),error)) return fail();
  if (!validate_runtime_applications(options.applications,error) ||
      !validate_projection_selection(options.projections,error)) return fail();
  if (!options.name || !*options.name || !options.timezone || !*options.timezone ||
      (options.controls.size && !options.controls.data) ||
      (options.services.size && !options.services.data) ||
      (options.declarations.size && !options.declarations.data)) {
    error = "invalid_device_options"; return fail();
  }
  self.controls.resize(options.controls.size);
  for (size_t i = 0; i < options.controls.size; ++i) {
    const auto &definition = options.controls.data[i];
    if (!definition.id || !*definition.id || !definition.label ||
        !definition.read || !definition.write) { error = "invalid_boolean_control"; return fail(); }
    for (const char *p = definition.id; *p; ++p) {
      if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_')) {
        error = "invalid_boolean_control_id"; return fail();
      }
    }
    auto &control = self.controls[i];
    control.owner = &self;
    control.definition = &definition;
    control.read = {Impl::read_control, &control};
    control.write = {Impl::write_control, &control};
    control.operations[0] = make_device_json_binding("state.get", control.read, false);
    control.operations[1] = make_device_json_binding("state.set", control.write, true);
    if (definition.diagnostic_gpio >= 0) {
      if (definition.diagnostic_gpio >= 64) { error = "invalid_boolean_control_gpio"; return fail(); }
      control.diagnostic = {Impl::observe_control, &control};
      for (auto &operation : control.operations)
        operation.projection.diagnostic_binding = &control.diagnostic;
    }
    control.capability = {definition.id};
    control.metadata = {definition.id, "boolean", nullptr, definition.label};
    control.module = {definition.id, &control.metadata, control.operations, 2};
  }
  struct Boot { Impl &self; const DeviceOptions &options; } boot{self, options};
  ProductRuntimeHooks hooks;
  hooks.context = &boot;
  hooks.preset = options.preset;
  hooks.boot_healthy_period_ms = options.boot_healthy_period_ms;
  hooks.health=options.health;
  hooks.startup_observer = options.startup_observer;
  hooks.startup_observer_context = options.startup_observer_context;
  hooks.provisioning=options.provisioning;
  hooks.bind_main_and_network_to_caller = true;
  hooks.register_services = [](void *opaque, std::string &error) {
    auto &[self, options] = *static_cast<Boot *>(opaque);
    if(!self.installed_applications.configure_devices(options.application_devices,error))return false;
    if (!configure_managed_assets(options.assets.data, options.assets.size, error)) return false;
    std::vector<ServiceNode> services;
    for (size_t i = 0; i < options.services.size; ++i) services.push_back(options.services.data[i]);
    std::vector<ProductDeclarationNode> declarations{{"device", Impl::declare, &self}};
    for (size_t i = 0; i < options.declarations.size; ++i) declarations.push_back(options.declarations.data[i]);
    if (!append_projection_declarations(self.projections, options.projections, declarations, error)) return false;
    RuntimeExecutionDefaults defaults{self.execution_owners,{options.application_foreground_context,options.application_foreground_transition}};
    if (!install_product_composition(self.context, self.projections, self.installed_applications,
        {options.name, services.data(), services.size(), declarations.data(), declarations.size()}, error, selected_runtime_preset(options.preset),&defaults)) return false;
    if (options.module_maintenance.size && !self.module_maintenance.configure(
        options.module_maintenance.data,options.module_maintenance.size,self.runtime,error)) return false;
#if CONFIG_LUNAREDU_DEVICE_WEB
    auto web = options.web;
    if (!web.product_name) web.product_name = options.name;
    if (!configure_device_web(web, error)) return false;
#endif
    return true;
  };
  hooks.start_executor_tasks = [](void *opaque, std::string &error) {
    auto &self = static_cast<Boot *>(opaque)->self;
    // Admission may outlive a startup timeout: retain the owner from this point.
    self.worker_admitted = true;
    (void)error;
    return true;
  };
  const auto &preset=selected_runtime_preset(options.preset);
  std::vector<ServiceExecutorDefinition> executors;
  executors.reserve(3+preset.execution.size());
  executors.push_back({kMainServiceExecutor,"product.main",8});
  executors.push_back({kNetworkServiceExecutor,"network.runtime",8});
  executors.push_back(maintenance_io_executor());
  for(const auto *provider:preset.execution)if(provider->executor.id)executors.push_back(provider->executor);
  if (!ProductRuntimeAccess::bootstrap(self.context, self.runtime, self.installed_applications, self.projections,
      executors.data(),executors.size(),
      options.network, options.timezone, hooks, nullptr, error)) return fail();
  if (options.initialize && !options.initialize(error)) return fail();
  // Native products may finish registering their launcher after start(), before
  // the first poll. There is still one device-owned foreground controller.
  if (options.enable_applications && !self.context.application()) {
    if (!self.applications.ensure(self.context, options.applications, error)) return fail();
    if (!register_default_foreground(*self.applications.get(), options.name, error)) return fail();
  }
  self.ready = true;
  return true;
}
void Device::poll(bool application_healthy,DevicePollScope scope) {
  auto &self = *impl_;
  if (!self.runtime.current_task_owns_executor(kMainServiceExecutor)) return;
  poll_update_maintenance(self.context,kMainServiceExecutor);
  if (self.applications.get() && !update_maintenance_active(self.context)) {
    std::string error;
    if (!self.applications.poll("system.idle", self.ready, error) && self.ready) {
      self.ready = false;
      self.startup_error = error;
      ESP_LOGE("device", "%s", self.startup_error.c_str());
    }
  }
  self.installed_applications.poll();
  const bool foreground_ready = !self.applications.get() || self.applications.ready();
  if (self.attempted) poll_maintenance_runtime(self.context, self.maintenance,
      self.ready && foreground_ready && application_healthy,!self.foreground_io_checkpoints);
  if (scope==DevicePollScope::Complete) poll_modules();
}
void Device::poll_modules() {
  auto &self=*impl_;
  if (self.ready && !self.module_maintenance.empty() && self.runtime.current_task_owns_executor(kMainServiceExecutor))
    self.module_maintenance.poll(self.runtime,uint32_t(esp_timer_get_time()/1000));
}
void Device::use_foreground_io_checkpoints(){
  if(impl_->runtime.current_task_owns_executor(kMainServiceExecutor))impl_->foreground_io_checkpoints=impl_->ready;
}
void Device::poll_foreground_io(){
  if(impl_->runtime.current_task_owns_executor(kMainServiceExecutor) && impl_->foreground_io_checkpoints)
    poll_usb_device_api(impl_->projections);
}
[[noreturn]] void Device::run(const DeviceOptions &options) {
  std::string error;
  if (!start(options, error)) ESP_LOGE("device", "%s", error.c_str());
  for (;;) { poll(); vTaskDelay(pdMS_TO_TICKS(kNetworkPollIntervalMs)); }
}
} // namespace lunaredu::lunaros
