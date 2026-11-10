#include "app/lifecycle/app_controller_view.h"
#include "runtime/application_host.h"
#include "app/lifecycle/app_controller.h"
#include "runtime/device_context.h"
#include <new>
#include <vector>
#include <cstring>
#include <cstdlib>
#include "kernel/runtime.h"
#include "diagnostics/cpu_monitor.h"
#include "diagnostics/elapsed_work_report.h"
#include "platform/system.h"

namespace lunaredu::lunaros {
bool validate_runtime_applications(app::RuntimeApplicationSelection selection, std::string &error) {
  if ((selection.size && !selection.data) || selection.size > app::kForegroundCapacity) {
    error="invalid_runtime_application_selection"; return false;
  }
  for (size_t i=0;i<selection.size;++i) {
    const auto &entry=selection.data[i];
    if (!entry.id || !*entry.id || !entry.create) { error="invalid_runtime_application_entry"; return false; }
    for(size_t j=0;j<i;++j)if(!std::strcmp(selection.data[j].id,entry.id)) {
      error="duplicate_runtime_application:"+std::string(entry.id); return false;
    }
  }
  error.clear(); return true;
}
struct ApplicationHost::Impl {
  app::AppControllerStorage applications;
  DeviceContext *device = nullptr;
  bool attempted = false;
  bool startup_completed = false;
  bool current_ready = false;
  std::string failure;
  app::RuntimeApplicationSelection selection;
  std::vector<std::unique_ptr<app::RuntimeApplicationInstance>> owners;
  system::ElapsedWorkTiming<2> work;
  system::ElapsedWorkReporter<2> reporter;
};
ApplicationHost::ApplicationHost() = default;
ApplicationHost::~ApplicationHost() {
  std::string error; if (!reset(error)) std::abort();
}
bool ApplicationHost::reset(std::string &error) {
  error.clear();
  if (!impl_) return true;
  // Rejected allocation/metadata/coordinator admission retained no callbacks.
  // Its empty host can retire without touching another live composition.
  if (!get() && impl_->owners.empty()) { impl_.reset(); return true; }
  if (get() && get()->setup_started()) {
    error="application_host_retirement_required"; return false;
  }
  if (!impl_->device->runtime.can_reset_composition(error) ||
      !impl_->device->model.control.can_reset(error)) return false;
  impl_->applications.reset();
  impl_.reset(); return true;
}
app::AppController *ApplicationHost::get() const {
  return impl_ ? impl_->applications.get() : nullptr;
}
bool ApplicationHost::ready() const { return impl_ && impl_->current_ready; }
bool ApplicationHost::ensure(DeviceContext &device, app::RuntimeApplicationSelection selection, std::string &error) {
  if (impl_) {
    if (impl_->device != &device) { error = "application_host_owner_mismatch"; return false; }
    if (impl_->selection.data != selection.data || impl_->selection.size != selection.size) {
      error = "application_host_selection_mismatch"; return false;
    }
    error = impl_->failure;
    return error.empty();
  }
  if (device.application()) { error="application_host_controller_conflict"; return false; }
  impl_.reset(new (std::nothrow) Impl);
  if (!impl_) { error = "application_host_allocation_failed"; return false; }
  auto &self = *impl_;
  self.device = &device;
  self.selection = selection;
  const auto fail = [&] (const std::string &message) { self.failure = message; error = message; return false; };
  if (!validate_runtime_applications(selection,error)) return fail(error);
  if (!self.applications.ensure(device, error)) {
    self.failure = error.empty() ? "application_controller_allocation_failed" : error;
    error = self.failure; return false;
  }
  app::CapabilityExecutionPort execution;
  execution.context=&device.runtime;
  execution.owns=[](void *opaque,OperationExecutor target){
    return static_cast<KernelRuntime *>(opaque)->current_task_owns_executor(
      target==OperationExecutor::NetworkIo?kNetworkIoServiceExecutor:kMainServiceExecutor);
  };
  execution.invoke=[](void *opaque,OperationExecutor target,void (*callback)(void *),void *context,
      uint32_t timeout,std::string &failure){
    struct Invocation {void (*callback)(void *);void *context;} invocation{callback,context};
    return static_cast<KernelRuntime *>(opaque)->invoke_executor(
      target==OperationExecutor::NetworkIo?kNetworkIoServiceExecutor:kMainServiceExecutor,
      [](void *value,ServiceOperationResult &){auto &work=*static_cast<Invocation *>(value);work.callback(work.context);},
      &invocation,failure,timeout);
  };
  if(!self.applications->capabilities().configure_capability_execution(execution,error))return fail(error);
  if(!app::declare_application_launch_capability(self.applications->capabilities(),device,error))return fail(error);
  for (size_t i = 0; i < selection.size; ++i) {
    const auto &entry = selection.data[i];
    auto owner = entry.create(*self.applications.get(), device);
    if (!owner) return fail("runtime_application_allocation_failed:" + std::string(entry.id));
    // Registration may retain callbacks even after failure. Retain their owner
    // until the entire composition is reset, as for desktop App providers.
    self.owners.push_back(std::move(owner));
    if (!self.owners.back()->declare(error))
      return fail(error.empty() ? "runtime_application_declaration_failed:" + std::string(entry.id) : error);
  }
  error.clear(); return true;
}
bool ApplicationHost::poll(const char *initial_application, bool start_allowed,
                           std::string &error) {
  if (!get()) { error = "application_host_unavailable"; return false; }
  auto &self = *impl_;
  if (!self.device->runtime.current_task_owns_executor(kMainServiceExecutor)) {
    error="application_host_executor_owner_mismatch"; return false;
  }
  auto &controller = *self.applications.get();
  self.work.enable(system::main_work_profiling_active());
  self.reporter.report("runtime.foreground",self.work.snapshot(),{"controller_tick","progress"});
  if (start_allowed && !self.attempted && self.failure.empty()) {
    self.attempted = true;
    if (!controller.setup_started() &&
        (!initial_application || !*initial_application ||
         !controller.setup(initial_application, error)))
      self.failure = error.empty() ? "foreground_setup_failed" : error;
  }
  // Cleanup keeps running after rejected or partially admitted startup. A
  // failed boot never becomes healthy simply because cleanup later succeeds.
  if (controller.setup_started())self.work.measure(0,system_monotonic_us,[&]{controller.tick();});
  self.current_ready = false;
  if (self.failure.empty() && controller.setup_started()) {
    const auto status = self.work.measure(1,system_monotonic_us,[&]{return lunaredu::app::AppControllerView{controller}.progress();});
    self.current_ready = status.state == app::AppControllerState::Running &&
        status.transition_phase == app::AppTransitionPhase::Idle;
    if (!self.startup_completed && (status.state == app::AppControllerState::Error ||
        status.transition_phase == app::AppTransitionPhase::StoppingFailedTarget ||
        status.transition_phase == app::AppTransitionPhase::StoppingFailedRestore ||
        status.transition_phase == app::AppTransitionPhase::RestoringPrevious)) {
      app::AppControllerStatusSnapshot detail;
      controller.status_snapshot_into(detail);
      self.failure = detail.last_error.empty() ? "foreground_start_failed" : detail.last_error;
    }
    if (self.current_ready) self.startup_completed = true;
  }
  error = self.failure;
  return error.empty();
}
} // namespace lunaredu::lunaros
