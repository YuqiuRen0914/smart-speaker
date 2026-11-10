#include "kernel/service_maintenance.h"
#include "app/lifecycle/snapshot_names.h"
#include "app/execution/application_context.h"
#include "app/execution/application_maintenance.h"
#include "kernel/runtime.h"
#include <cstdlib>
#include <cstring>
#include <new>
#include <utility>
#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#include "diagnostics/cpu_monitor.h"
#include "platform/system.h"
#endif
#include "app/lifecycle/app_controller.h"
#include "app/lifecycle/app_commands.h"

#include "model/device_model.h"


namespace lunaredu::app {
bool AppController::foreground_current(const char *id) const {
  return id && transition_.phase==AppTransitionPhase::Idle &&
      maintenance_restore_.app_id.empty() && active_application_ && active_application_->id==std::string_view(id);
}

AppControllerStorage::~AppControllerStorage() { reset(); }

bool AppControllerStorage::ensure(lunaros::ApplicationContext &device,
                                  std::string &error) {
  error.clear();
  if (controller_ != nullptr) {
    if (device.application() != controller_) { error="application_controller_owner_mismatch"; return false; }
    return true;
  }
  if (device.application()) { error="application_controller_already_bound"; return false; }
  if (!lunaros::ServiceMaintenanceAccess::can_bind(device.runtime,error)) return false;
#ifdef ESP_PLATFORM
  void *storage = heap_caps_malloc(sizeof(AppController),
                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
  void *storage = std::malloc(sizeof(AppController));
#endif
  if (storage == nullptr) {
    error = "app_controller_allocation_failed";
    return false;
  }
  controller_ = new (storage) AppController(device);
  error.clear();
  return true;
}

void AppControllerStorage::reset() {
  if (controller_ == nullptr) return;
  controller_->~AppController();
#ifdef ESP_PLATFORM
  heap_caps_free(controller_);
#else
  std::free(controller_);
#endif
  controller_ = nullptr;
}

AppController::AppController(lunaros::ApplicationContext &device) : device_(device) {
  // Two controllers for one device are a composition error, before workers start.
  if (device_.application_ != nullptr) std::abort();
  device_.application_ = this;
  
  
  std::string error;
  if (!lunaros::ServiceMaintenanceAccess::bind(device_.runtime,
          {this,
           [](void *context, const char *id, std::string &failure) {
             return static_cast<AppController *>(context)->before_service_restart(id, failure);
           },
           [](void *context, const char *id, bool restarted, std::string &failure) {
             return static_cast<AppController *>(context)->after_service_restart(id, restarted, failure);
           }}, error)) std::abort();
}
AppController::~AppController() {
  for(size_t i=0;i<application_count_;++i) {
    auto &a=applications_[i];
    if(a.execution_lease.generation) (void)device_.runtime.execution_admission().release(a.resource_owner_id.c_str(),a.execution_lease);
    if(a.capability_execution_lease.generation) (void)device_.runtime.execution_admission().release(a.resource_owner_id.c_str(),a.capability_execution_lease);
  }
  for (size_t i = 0; i < application_count_; ++i)
    (void)device_.runtime.forget_task_owner(lunaros::ManagedTaskOwnerKind::Application, applications_[i].id.c_str());
  lunaros::ServiceMaintenanceAccess::unbind(device_.runtime,this);
  device_.model.capabilities.deactivate(kApplicationCapability.id);
  device_.application_ = nullptr;
  
  
}

const char *app_controller_state_name(AppControllerState state) {
  switch (state) {
  case AppControllerState::Stopped:
    return "stopped";
  case AppControllerState::Switching:
    return "switching";
  case AppControllerState::Running:
    return "running";
  case AppControllerState::Error:
    return "error";
  }
  return "unknown";
}

const char *app_transition_phase_name(AppTransitionPhase phase) {
  switch (phase) {
  case AppTransitionPhase::Idle:
    return "idle";
  case AppTransitionPhase::StartingTarget:
    return "starting_target";
  case AppTransitionPhase::RestoringPrevious:
    return "restoring_previous";
  case AppTransitionPhase::StoppingFailedTarget:
    return "stopping_failed_target";
  case AppTransitionPhase::StoppingFailedRestore:
    return "stopping_failed_restore";
  }
  return "idle";
}

bool AppController::setup(const std::string &initial_application_id,
                          std::string &error) {
  error.clear();
  if (application_count_ == 0) {
    error = "application_registry_empty";
    state_ = AppControllerState::Error;
    last_error_ = error;
    return false;
  }
  if (initial_application_id.empty()) {
    error = "missing_initial_application_id";
    state_ = AppControllerState::Error;
    last_error_ = error;
    return false;
  }

  if (!seal(error)) return false;
  setup_started_ = true;
  if (!active_application_ && !transition_.target &&
      !switch_to(initial_application_id, error)) {
    state_ = AppControllerState::Error;
    last_error_ = error;
    return false;
  }
  // The controller remains available after stopping a foreground application.
  // An omitted protocol contribution requires no capability or JSON dependency.
  if (device_.model.capabilities.declared(kApplicationCapability.id))
    return device_.model.capabilities.activate(kApplicationCapability, &error);
  return true;
}

void AppController::tick() {
#ifdef ESP_PLATFORM
  tick_work_.enable(system::main_work_profiling_active());
  tick_reporter_.report("application.tick",tick_work_.snapshot(),
      {"capabilities","transition","launches","availability","foreground"});
#endif
  const auto measure=[&](size_t stage,auto &&action)->decltype(auto){
#ifdef ESP_PLATFORM
    return tick_work_.measure(stage,lunaros::system_monotonic_us,std::forward<decltype(action)>(action));
#else
    (void)stage;return std::forward<decltype(action)>(action)();
#endif
  };
  measure(0,[&]{for(size_t i=0;i<capability_tick_count_;++i) {
    auto &owner=*capability_ticks_[i];owner.capability_tick(owner.context);
  }});
  if (transition_.target != nullptr) {
    measure(1,[&]{advance_transition();});
  }
  measure(2,[&]{poll_launches();});
  if (active_application_ != nullptr) {
    std::string unavailable_reason;
    if (!measure(3,[&]{return application_available(*active_application_, unavailable_reason);})) {
      if (!stop("application_dependency_unavailable")) return;
      state_ = AppControllerState::Error;
      last_error_ = unavailable_reason;
      return;
    }
  }
  if (active_application_ == nullptr && transition_.target == nullptr &&
      !maintenance_restore_.app_id.empty() &&
      device_.runtime.service_available(maintenance_restore_.service_id.c_str())) {
    const std::string restore_id = maintenance_restore_.app_id;
    maintenance_restore_.clear();
    std::string restore_error;
    if (!switch_to(restore_id, ApplicationEnterReason::User, restore_error)) {
      state_ = AppControllerState::Error;
      last_error_ = "application_restore_failed:" + restore_error;
      return;
    }
  }
  if (active_application_ != nullptr && active_application_->tick != nullptr) {
    measure(4,[&]{active_application_->tick(active_application_->context);});
  }
}

bool AppController::enqueue_launch(const char *source, const char *id,
    ApplicationLaunch &token, std::string &error) {
  error.clear();
  if (!source || !*source || std::strlen(source)>64 || !id || !*id || std::strlen(id)>64) {
    error="invalid_application_launch"; return false;
  }
  if (token.generation) {
    ApplicationLaunchSnapshot snapshot;
    if(!launch_snapshot(source,token,snapshot,error))return false;
    if(snapshot.application_id!=id){error="application_launch_identity_conflict";return false;}
    return true;
  }
  if (!find_application(id)) { error="application_not_found"; return false; }
  LaunchRecord *slot=nullptr;
  for (auto &record:launches_) {
    if (!record.generation) {slot=&record;break;}
    if (record.snapshot.state!=ApplicationLaunchState::Queued && record.snapshot.state!=ApplicationLaunchState::Switching &&
        (!slot || record.generation<slot->generation)) slot=&record;
  }
  if (!slot || next_launch_generation_==UINT64_MAX) {error="application_launch_capacity";return false;}
  *slot={};slot->generation=token.generation=++next_launch_generation_;
  slot->source=source;slot->snapshot.application_id=id;
  slot->snapshot.state=ApplicationLaunchState::Queued;
  slot->expected_transition=transition_id_;return true;
}
bool AppController::launch_snapshot(const char *source, ApplicationLaunch token,
    ApplicationLaunchSnapshot &snapshot, std::string &error) const {
  error.clear();
  for (const auto &record:launches_) if (token.generation && record.generation==token.generation) {
    if (!source || record.source!=source) {error="application_launch_owner_mismatch";return false;}
    snapshot=record.snapshot;return true;
  }
  error="stale_application_launch";return false;
}
bool AppController::cancel_launch(const char *source, ApplicationLaunch token, std::string &error) {
  ApplicationLaunchSnapshot snapshot;if (!launch_snapshot(source,token,snapshot,error)) return false;
  if(snapshot.state==ApplicationLaunchState::Cancelled)return true;
  for(auto &record:launches_) if(record.generation==token.generation){
    if(record.cancelling)return true;
    if(snapshot.state==ApplicationLaunchState::Queued){record.snapshot.state=ApplicationLaunchState::Cancelled;return true;}
    if(snapshot.state==ApplicationLaunchState::Switching&&record.snapshot.transition_id==transition_id_&&transition_.phase==AppTransitionPhase::StartingTarget){
      record.cancelling=true;fail_target_and_restore("application_launch_cancelled");return true;
    }
  }
  error="application_launch_already_started";return false;
}
void AppController::poll_launches() {
  for (auto &record:launches_) if (record.generation && record.snapshot.state==ApplicationLaunchState::Switching) {
    if (transition_.phase!=AppTransitionPhase::Idle) continue;
    if(record.cancelling){record.snapshot.state=ApplicationLaunchState::Cancelled;record.snapshot.error.clear();continue;}
    const bool opened=active_application_ && active_application_->id==record.snapshot.application_id && transition_id_==record.snapshot.transition_id;
    record.snapshot.state=opened?ApplicationLaunchState::Opened:ApplicationLaunchState::Failed;
    if (!opened) record.snapshot.error=last_error_.empty()?"application_launch_superseded":last_error_;
  }
  if (!setup_started_ || transition_.phase!=AppTransitionPhase::Idle) return;
  LaunchRecord *next=nullptr;
  for(auto &record:launches_) if(record.generation && record.snapshot.state==ApplicationLaunchState::Queued &&
      (!next || record.generation<next->generation)) next=&record;
  if (!next) return;
  if (next->expected_transition!=transition_id_) {
    next->snapshot.state=ApplicationLaunchState::Failed;next->snapshot.error="application_launch_superseded";return;
  }
  std::string error;
  const bool accepted=switch_to(next->snapshot.application_id,ApplicationEnterReason::Service,error);
  next->snapshot.transition_id=transition_id_;
  next->snapshot.state=accepted?ApplicationLaunchState::Switching:ApplicationLaunchState::Failed;
  next->snapshot.error=error;
  if (accepted && transition_.phase==AppTransitionPhase::Idle)
    next->snapshot.state=ApplicationLaunchState::Opened;
}

bool AppController::acquire_resources(
    const std::string &owner_id,
    const lunaros::ResourceRequirement *requirements,
    size_t requirement_count, std::string &error) {
  const bool acquired = resource_registry_.acquire(
      owner_id, requirements, requirement_count, error);
  if (acquired) ++resource_revision_;
  return acquired;
}

void AppController::release_resources(const std::string &owner_id) {
  resource_registry_.release(owner_id);
  ++resource_revision_;
}

bool AppController::resource_owner_holds(
    const std::string &owner_id, const std::string &resource_id) const {
  return resource_registry_.owner_holds(owner_id, resource_id);
}

} // namespace lunaredu::app
