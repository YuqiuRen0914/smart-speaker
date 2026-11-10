#include "lunaredu_lunaros/app/app_controller.h"

#include "lunaredu_lunaros/kernel/log.h"

namespace lunaredu::app {
namespace {

constexpr const char *kLogTag = "lunaredu.app";

} // namespace

bool AppController::switch_to(const std::string &application_id,
                              std::string &error) {
  return switch_to(application_id, ApplicationEnterReason::User, error);
}

bool AppController::switch_to(const std::string &application_id,
                              ApplicationEnterReason reason,
                              std::string &error) {
  error.clear();
  RegisteredApplication *target = find_application(application_id);
  if (target == nullptr) {
    error = "application_not_found";
    return false;
  }

  if (transition_phase_ != AppTransitionPhase::Idle) {
    if (transition_phase_ == AppTransitionPhase::StartingTarget &&
        pending_application_ == target) {
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
    return false;
  }
  if (target->prepare != nullptr && !target->prepare(target->context, error)) {
    resource_registry_.cancel(target->resource_owner_id);
    if (error.empty()) {
      error = "application_prepare_failed";
    }
    return false;
  }

  ++transition_id_;
  last_error_.clear();
  previous_application_ = active_application_;
  previous_enter_reason_ = active_enter_reason_;
  active_application_ = nullptr;
  if (previous_application_ != nullptr) {
    stop_application(*previous_application_, "application_switch_prepare");
  }

  pending_application_ = target;
  pending_enter_reason_ = reason;
  transition_phase_ = AppTransitionPhase::StartingTarget;
  state_ = AppControllerState::Switching;
  advance_transition();

  if (transition_phase_ == AppTransitionPhase::RestoringPrevious ||
      (transition_phase_ == AppTransitionPhase::Idle &&
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

void AppController::stop_application(RegisteredApplication &application,
                                     const char *reason) {
  if (application.stop != nullptr) {
    application.stop(application.context, reason);
  }
}

void AppController::fail_target_and_restore(const std::string &error) {
  RegisteredApplication *failed_target = pending_application_;
  last_error_ = error.empty() ? "application_start_failed" : error;
  if (failed_target != nullptr) {
    LOGE(kLogTag, "application start failed id=%s error=%s",
         failed_target->id.c_str(), last_error_.c_str());
    stop_application(*failed_target, "application_start_failed");
    resource_registry_.cancel(failed_target->resource_owner_id);
  }
  pending_application_ = nullptr;

  if (previous_application_ == nullptr) {
    transition_phase_ = AppTransitionPhase::Idle;
    state_ = AppControllerState::Error;
    return;
  }

  pending_application_ = previous_application_;
  pending_enter_reason_ = previous_enter_reason_;
  previous_application_ = nullptr;
  transition_phase_ = AppTransitionPhase::RestoringPrevious;
  state_ = AppControllerState::Switching;
  advance_transition();
}

void AppController::finish_failed_restore(const std::string &error) {
  const std::string rollback_error =
      error.empty() ? "application_rollback_failed" : error;
  if (pending_application_ != nullptr) {
    LOGE(kLogTag, "application rollback failed id=%s error=%s",
         pending_application_->id.c_str(), rollback_error.c_str());
    stop_application(*pending_application_, "application_rollback_failed");
    resource_registry_.release(pending_application_->resource_owner_id);
  }
  pending_application_ = nullptr;
  previous_application_ = nullptr;
  transition_phase_ = AppTransitionPhase::Idle;
  state_ = AppControllerState::Error;
  last_error_ += ";rollback_failed:" + rollback_error;
}

void AppController::advance_transition() {
  if (pending_application_ == nullptr ||
      transition_phase_ == AppTransitionPhase::Idle) {
    return;
  }

  std::string error;
  const ApplicationStartResult result =
      pending_application_->start_with_reason != nullptr
          ? pending_application_->start_with_reason(
                pending_application_->context, pending_enter_reason_, error)
      : pending_application_->start != nullptr
          ? pending_application_->start(pending_application_->context, error)
          : ApplicationStartResult::Started;
  if (result == ApplicationStartResult::Pending) {
    return;
  }

  if (transition_phase_ == AppTransitionPhase::RestoringPrevious) {
    if (result == ApplicationStartResult::Failed) {
      finish_failed_restore(error);
      return;
    }
    active_application_ = pending_application_;
    active_enter_reason_ = pending_enter_reason_;
    pending_application_ = nullptr;
    transition_phase_ = AppTransitionPhase::Idle;
    state_ = AppControllerState::Running;
    LOGW(kLogTag, "previous application restored id=%s transition=%lu error=%s",
         active_application_->id.c_str(),
         static_cast<unsigned long>(transition_id_), last_error_.c_str());
    return;
  }

  if (result == ApplicationStartResult::Failed) {
    fail_target_and_restore(error);
    return;
  }

  const char *replacing_owner =
      previous_application_ != nullptr
          ? previous_application_->resource_owner_id.c_str()
          : nullptr;
  if (!resource_registry_.commit(pending_application_->resource_owner_id,
                                 replacing_owner, error)) {
    fail_target_and_restore(error.empty() ? "resource_commit_failed" : error);
    return;
  }

  active_application_ = pending_application_;
  active_enter_reason_ = pending_enter_reason_;
  pending_application_ = nullptr;
  previous_application_ = nullptr;
  transition_phase_ = AppTransitionPhase::Idle;
  state_ = AppControllerState::Running;
  last_error_.clear();
  LOGI(kLogTag, "active application=%s reason=%s transition=%lu",
       active_application_->id.c_str(),
       application_enter_reason_name(active_enter_reason_),
       static_cast<unsigned long>(transition_id_));
}

void AppController::stop(const char *reason) {
  const char *stop_reason =
      reason != nullptr && reason[0] != '\0' ? reason : "application_stop";
  ++transition_id_;

  if (pending_application_ != nullptr) {
    stop_application(*pending_application_, stop_reason);
    resource_registry_.release(pending_application_->resource_owner_id);
  }
  pending_application_ = nullptr;
  if (previous_application_ != nullptr) {
    resource_registry_.release(previous_application_->resource_owner_id);
  }
  previous_application_ = nullptr;
  if (active_application_ != nullptr) {
    stop_application(*active_application_, stop_reason);
    resource_registry_.release(active_application_->resource_owner_id);
    active_application_ = nullptr;
  }

  transition_phase_ = AppTransitionPhase::Idle;
  state_ = AppControllerState::Stopped;
  last_error_.clear();
}

} // namespace lunaredu::app
