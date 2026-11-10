#include "app/lifecycle/app_controller_view.h"
#include "kernel/runtime.h"
#include "app/lifecycle/app_controller.h"

#include "model/device_model.h"

namespace lunaredu::app {
AppControllerProgress AppControllerView::progress() const {
  return controller_ ? AppControllerProgress{controller_->state_, controller_->transition_.phase}
      : AppControllerProgress{AppControllerState::Stopped, AppTransitionPhase::Idle};
}

void AppController::status_snapshot_into(
    AppControllerStatusSnapshot &result) const {
  result.state = state_;
  result.transition_phase = transition_.phase;
  result.transition_id = transition_id_;
  result.last_error = last_error_;
  result.active_application_id.clear();
  result.pending_application_id.clear();
  result.previous_application_id.clear();
  if (active_application_ != nullptr) {
    result.active_application_id = active_application_->id;
  }
  if (transition_.target != nullptr) {
    result.pending_application_id = transition_.target->id;
  }
  if (transition_.previous != nullptr) {
    result.previous_application_id = transition_.previous->id;
  }
  result.application_count = application_count_;
}

bool AppController::application_snapshot_into(
    size_t index, ApplicationSnapshot &item) const {
  if (index >= application_count_) {
    return false;
  }

  const RegisteredApplication &application = applications_[index];
  item.id = application.id;
  item.title = application.title;
  item.icon = application.icon;
  item.launcher_accent_rgb = application.launcher_accent_rgb;
  item.version = application.version;
  item.settings_route = application.settings_route;
  item.kind = application.kind;
  item.backend = application.backend;
  item.system_ui = application.system_ui;
  item.is_home = application.is_home;
  item.capability_count = application.capability_count;
  item.unavailable_reason.clear();
  std::string unavailable_reason;
  item.available = application_available(application, unavailable_reason);
  item.unavailable_reason = unavailable_reason;
  item.has_preparation = application.preparation != nullptr;
  item.preparation = application.preparation ? application.preparation->status() : ApplicationPreparationStatus{};
  for (size_t capability_index = 0;
       capability_index < application.capability_count; ++capability_index) {
    const auto &source = application.capabilities[capability_index];
    auto &target = item.capabilities[capability_index];
    target.id = source.id.c_str();
    target.required = source.required;
    target.available =
        device_.model.capabilities.active(source.id.c_str());
  }
  item.service_count = application.service_count;
  for (size_t service_index = 0;
       service_index < application.service_count; ++service_index) {
    const auto &source = application.services[service_index];
    auto &target = item.services[service_index];
    target.id = source.id.c_str();
    target.required = source.required;
    target.available = device_.runtime.service_available(source.id.c_str());
  }
  item.resource_count = application.resource_count;
  for (size_t resource_index = 0;
       resource_index < application.resource_count; ++resource_index) {
    item.resources[resource_index].id =
        application.resources[resource_index].id;
    item.resources[resource_index].access =
        application.resources[resource_index].access;
  }
  item.active = &application == active_application_;
  item.pending = &application == transition_.target;
  return true;
}

void AppController::snapshot_into(AppControllerSnapshot &result) const {
  status_snapshot_into(result);
  for (size_t index = 0; index < application_count_; ++index) {
    (void)application_snapshot_into(index, result.applications[index]);
  }
}


} // namespace lunaredu::app

namespace lunaredu::app {
bool AppControllerView::has_applications() const {
  if (!controller_) return false;
  for (size_t i=0;i<controller_->application_count_;++i)
    if (controller_->applications_[i].kind == ForegroundKind::Application) return true;
  return false;
}
const char *AppControllerView::active_application_id() const {
  return controller_ && controller_->active_application_ ? controller_->active_application_->id.c_str() : nullptr;
}
bool AppControllerView::foreground_current(std::string_view id) const {
  return controller_ && controller_->transition_.phase == AppTransitionPhase::Idle &&
      controller_->maintenance_restore_.app_id.empty() && controller_->active_application_ &&
      controller_->active_application_->id == id;
}
}
