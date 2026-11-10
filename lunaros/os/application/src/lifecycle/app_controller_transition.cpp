#include "app/lifecycle/app_controller_view.h"
#include "app/lifecycle/app_controller.h"
#include "kernel/runtime.h"

#include <esp_log.h>
#include <esp_timer.h>

namespace lunaredu::app {
namespace {

constexpr const char *application_enter_reason_name(
    ApplicationEnterReason reason) {
  switch (reason) {
  case ApplicationEnterReason::Boot:
    return "boot";
  case ApplicationEnterReason::User:
    return "user";
  case ApplicationEnterReason::Service:
    return "service";
  }
  return "user";
}

constexpr const char *kLogTag = "lunaredu.app";

} // namespace

void AppController::finish_transition(AppControllerState state,bool clear_error) {
  transition_={}; state_=state;
  if(clear_error) last_error_.clear();
}

bool AppController::switch_to(std::string_view application_id,
                              std::string &error) {
  return switch_to(application_id, ApplicationEnterReason::User, error);
}

bool AppController::switch_to(std::string_view application_id,
                              ApplicationEnterReason reason,
                              std::string &error) {
  error.clear();
  // Reconciliation of an unchanged foreground requires no catalog lookup.
  // Pending transitions and maintenance restoration still take the full path.
  if (AppControllerView{*this}.foreground_current(application_id)) return true;
  if (!maintenance_restore_.app_id.empty() &&
      maintenance_restore_.app_id != application_id) {
    maintenance_restore_.clear();
  }
  RegisteredApplication *target = find_application(application_id);
  if (target == nullptr) {
    error = "application_not_found";
    return false;
  }

  if (transition_.phase != AppTransitionPhase::Idle) {
    if (transition_.phase == AppTransitionPhase::StartingTarget &&
        transition_.target == target) {
      return true;
    }
    error = "application_switch_in_progress";
    return false;
  }
  if (active_application_ == target) {
    return true;
  }
  if (!application_available(*target, error)) {
    return false;
  }
  if (!acquire_application_execution(*target,error)) return false;

  std::array<lunaros::ResourceRequirement, kApplicationResourceCapacity>
      requirements;
  copy_resource_requirements(*target, requirements);
  const char *replacing_owner =
      active_application_ != nullptr
          ? active_application_->resource_owner_id.c_str()
          : nullptr;
  if (!resource_registry_.reserve(target->resource_owner_id,
                                  requirements.data(), target->resource_count,
                                  replacing_owner, error)) {
    (void)device_.runtime.execution_admission().release(target->resource_owner_id.c_str(),target->execution_lease);
    return false;
  }
  ++resource_revision_;
  if (target->prepare != nullptr && !target->prepare(target->context, error)) {
    (void)device_.runtime.execution_admission().release(target->resource_owner_id.c_str(),target->execution_lease);
    resource_registry_.cancel(target->resource_owner_id);
    ++resource_revision_;
    if (error.empty()) {
      error = "application_prepare_failed";
    }
    return false;
  }

  ++transition_id_;
  last_error_.clear();
  transition_.failure.clear();
  if (active_application_ && !stop_application(*active_application_, "application_switch_prepare")) {
    (void)device_.runtime.execution_admission().release(target->resource_owner_id.c_str(),target->execution_lease);
    resource_registry_.cancel(target->resource_owner_id);
    ++resource_revision_;
    error = last_error_;
    return false;
  }
  transition_.previous = active_application_;
  transition_.previous_reason = active_enter_reason_;
  active_application_ = nullptr;

  transition_.target = target;
  transition_.target_reason = reason;
  transition_.phase = AppTransitionPhase::StartingTarget;
  transition_.started_us=esp_timer_get_time();
  state_ = AppControllerState::Switching;
  advance_transition();

  if (transition_.phase == AppTransitionPhase::RestoringPrevious ||
      transition_.phase == AppTransitionPhase::StoppingFailedTarget ||
      transition_.phase == AppTransitionPhase::StoppingFailedRestore ||
      (transition_.phase == AppTransitionPhase::Idle &&
       active_application_ != target)) {
    error = last_error_.empty() ? "application_start_failed" : last_error_;
    return false;
  }
  if (state_ == AppControllerState::Error) {
    error = last_error_;
    return false;
  }
  return true;
}

bool AppController::stop_application(RegisteredApplication &application,
                                     const char *reason) {
  std::string error;
  if (application.stop_checked && !application.stop_checked(application.context, reason, error)) {
    last_error_ = error.empty() ? "application_stop_failed" : error;
    return false;
  }
  if (application.stop != nullptr) {
    application.stop(application.context, reason);
  }
  for(size_t i=0;i<application.service_count;++i){
    auto &service=application.services[i];
    if(!device_.runtime.release_service_use(application.resource_owner_id.c_str(),service.token,error)){
      last_error_=error;return false;
    }
  }
  for(size_t i=0;i<application.execution_resource_count;++i){
    if(!device_.runtime.release_service_resource(application.execution_resources[i].window,error)){
      last_error_=error;return false;
    }
  }
  if(application.execution_lease.generation)
    (void)device_.runtime.execution_admission().release(application.resource_owner_id.c_str(),application.execution_lease);
  return true;
}
bool AppController::acquire_application_execution(RegisteredApplication &application,std::string &error) {
  if(application.execution_lease.generation) return true;
  const char *reason=nullptr;
  if(!device_.runtime.execution_admission().reserve(application.resource_owner_id.c_str(),
      device_.runtime.current_task_execution(application.execution),application.execution_lease,reason)) {
    error=reason?reason:"application_execution_not_admitted";return false;
  }
  return true;
}
bool AppController::publish_application_uses(RegisteredApplication &application,std::string &error){
  for(size_t i=0;i<application.service_count;++i){auto &service=application.services[i];
    if(service.use&&!device_.runtime.request_service_use(service.id.c_str(),application.resource_owner_id.c_str(),service.token,error)){
      if(service.required)return false;
      error.clear();
    }
  }
  return true;
}

void AppController::fail_target_and_restore(const std::string &error) {
  RegisteredApplication *failed_target = transition_.target;
  last_error_ = error.empty() ? "application_start_failed" : error;
  if (failed_target != nullptr) {
    ESP_LOGE(kLogTag, "application start failed id=%s error=%s",
         failed_target->id.c_str(), last_error_.c_str());
    transition_.failure = last_error_;
    if (!stop_application(*failed_target, "application_start_failed")) {
      last_error_ = transition_.failure + ";cleanup_pending:" + last_error_;
      transition_.phase = AppTransitionPhase::StoppingFailedTarget;
      state_ = AppControllerState::Error;
      return;
    }
    last_error_ = transition_.failure;
    resource_registry_.cancel(failed_target->resource_owner_id);
    ++resource_revision_;
  }
  transition_.target = nullptr;

  if (transition_.previous == nullptr) {
    finish_transition(AppControllerState::Error);
    return;
  }

  transition_.target = transition_.previous;
  transition_.target_reason = transition_.previous_reason;
  transition_.previous = nullptr;
  transition_.phase = AppTransitionPhase::RestoringPrevious;
  transition_.started_us=esp_timer_get_time();
  state_ = AppControllerState::Switching;
  advance_transition();
}

void AppController::finish_failed_restore(const std::string &error) {
  const std::string rollback_error =
      error.empty() ? "application_rollback_failed" : error;
  if (transition_.target != nullptr) {
    ESP_LOGE(kLogTag, "application rollback failed id=%s error=%s",
         transition_.target->id.c_str(), rollback_error.c_str());
    transition_.failure = rollback_error;
    if (!stop_application(*transition_.target, "application_rollback_failed")) {
      last_error_ = "rollback_failed:" + transition_.failure + ";cleanup_pending:" + last_error_;
      transition_.phase = AppTransitionPhase::StoppingFailedRestore;
      state_ = AppControllerState::Error;
      return;
    }
    resource_registry_.release(transition_.target->resource_owner_id);
    ++resource_revision_;
  }
  finish_transition(AppControllerState::Error);
  last_error_ += ";rollback_failed:" + rollback_error;
}

void AppController::advance_transition() {
  if (transition_.target == nullptr ||
      transition_.phase == AppTransitionPhase::Idle) {
    return;
  }

  std::string error;
  if (transition_.phase == AppTransitionPhase::StoppingFailedTarget) {
    fail_target_and_restore(transition_.failure); return;
  }
  if (transition_.phase == AppTransitionPhase::StoppingFailedRestore) {
    finish_failed_restore(transition_.failure); return;
  }
  if(esp_timer_get_time()-transition_.started_us>=int64_t(transition_.target->startup_timeout_ms)*1000){
    if(transition_.phase==AppTransitionPhase::RestoringPrevious)finish_failed_restore("application_rollback_timeout");
    else fail_target_and_restore("application_start_timeout");
    return;
  }
  if (!application_available(*transition_.target, error)) {
    if (transition_.phase == AppTransitionPhase::RestoringPrevious) {
      finish_failed_restore(error);
    } else {
      fail_target_and_restore(error);
    }
    return;
  }
  if(!publish_application_uses(*transition_.target,error)){
    if(transition_.phase==AppTransitionPhase::RestoringPrevious)finish_failed_restore(error);
    else fail_target_and_restore(error);
    return;
  }
  for(size_t i=0;i<transition_.target->execution_resource_count;++i){
    auto &resource=transition_.target->execution_resources[i];
    if(!device_.runtime.reserve_service_resource(resource.id.c_str(),transition_.target->resource_owner_id.c_str(),resource.window,error)){
      if(error.rfind("service_resource_pending:",0)==0)return;
      if(transition_.phase==AppTransitionPhase::RestoringPrevious)finish_failed_restore(error);
      else fail_target_and_restore(error);
      return;
    }
  }
  if(!acquire_application_execution(*transition_.target,error)) {
    if(transition_.phase==AppTransitionPhase::RestoringPrevious)finish_failed_restore(error);
    else fail_target_and_restore(error);
    return;
  }
  const ApplicationStartResult result = transition_.target->start
      ? transition_.target->start(transition_.target->context, transition_.target_reason, error)
      : ApplicationStartResult::Started;
  if (result == ApplicationStartResult::Pending) {
    return;
  }

  if (transition_.phase == AppTransitionPhase::RestoringPrevious) {
    if (result == ApplicationStartResult::Failed) {
      finish_failed_restore(error);
      return;
    }
    active_application_ = transition_.target;
    active_enter_reason_ = transition_.target_reason;
    finish_transition(AppControllerState::Running);
    ESP_LOGW(kLogTag, "previous application restored id=%s transition=%lu error=%s",
         active_application_->id.c_str(),
         static_cast<unsigned long>(transition_id_), last_error_.c_str());
    return;
  }

  if (result == ApplicationStartResult::Failed) {
    fail_target_and_restore(error);
    return;
  }

  const char *replacing_owner =
      transition_.previous != nullptr
          ? transition_.previous->resource_owner_id.c_str()
          : nullptr;
  if (!resource_registry_.commit(transition_.target->resource_owner_id,
                                 replacing_owner, error)) {
    fail_target_and_restore(error.empty() ? "resource_commit_failed" : error);
    return;
  }
  ++resource_revision_;

  active_application_ = transition_.target;
  active_enter_reason_ = transition_.target_reason;
  finish_transition(AppControllerState::Running,true);
  ESP_LOGI(kLogTag, "active application=%s reason=%s transition=%lu",
       active_application_->id.c_str(),
       application_enter_reason_name(active_enter_reason_),
       static_cast<unsigned long>(transition_id_));
}

bool AppController::stop(const char *reason) {
  // Confirm quiescence before releasing any active foreground resource.
  if (active_application_ && !stop_application(*active_application_, reason)) return false;
  if (transition_.target && !stop_application(*transition_.target, reason)) return false;
  maintenance_restore_.clear();
  ++transition_id_;

  if (transition_.target != nullptr) {
    resource_registry_.release(transition_.target->resource_owner_id);
    ++resource_revision_;
  }
  transition_.target = nullptr;
  if (transition_.previous != nullptr) {
    resource_registry_.release(transition_.previous->resource_owner_id);
    ++resource_revision_;
  }
  transition_.previous = nullptr;
  if (active_application_ != nullptr) {
    resource_registry_.release(active_application_->resource_owner_id);
    ++resource_revision_;
    active_application_ = nullptr;
  }

  finish_transition(AppControllerState::Stopped,true);
  return true;
}

} // namespace lunaredu::app
