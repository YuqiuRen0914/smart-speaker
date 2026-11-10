#include "lunaredu_lunaros/app/app_controller.h"

#include "lunaredu_lunaros/model/device_model.h"
#include "capability_registration.h"

namespace lunaredu::app {

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
  }
  return "idle";
}

bool AppController::register_foreground(
    const ForegroundDefinition &application, std::string &error) {
  error.clear();
  if (setup_started_) {
    error = "application_registry_locked";
    return false;
  }
  if (application.id == nullptr || application.id[0] == '\0') {
    error = "invalid_application_id";
    return false;
  }
  if (application_count_ >= applications_.size()) {
    error = "application_registry_full";
    return false;
  }
  if (find_application(application.id) != nullptr) {
    error = "application_already_registered";
    return false;
  }
  if (application.manifest.capability_count > kApplicationCapabilityCapacity) {
    error = "application_capability_registry_full";
    return false;
  }
  if (application.manifest.capability_count > 0 &&
      application.manifest.capabilities == nullptr) {
    error = "invalid_application_capabilities";
    return false;
  }
  if (application.manifest.service_count > kApplicationServiceCapacity) {
    error = "application_service_registry_full";
    return false;
  }
  if (application.manifest.service_count > 0 &&
      application.manifest.services == nullptr) {
    error = "invalid_application_services";
    return false;
  }
  if (application.manifest.resource_count > kApplicationResourceCapacity) {
    error = "application_resource_registry_full";
    return false;
  }
  if (application.manifest.resource_count > 0 &&
      application.manifest.resources == nullptr) {
    error = "invalid_application_resources";
    return false;
  }

  // Prepare and validate the unpublished slot before invoking application code.
  RegisteredApplication &registered = applications_[application_count_];
  registered.id = application.id;
  registered.resource_owner_id = "app:" + registered.id;
  registered.title = application.manifest.title != nullptr &&
                             application.manifest.title[0] != '\0'
                         ? application.manifest.title
                         : application.id;
  registered.icon = application.manifest.icon != nullptr
                        ? application.manifest.icon
                        : "app";
  registered.launcher_accent_rgb = application.manifest.launcher_accent_rgb;
  registered.version = application.manifest.version != nullptr
                           ? application.manifest.version
                           : "1";
  registered.kind = application.manifest.kind;
  registered.backend = application.backend;
  registered.visibility = application.manifest.visibility;
  registered.system_ui = application.manifest.system_ui;
  registered.capability_count = application.manifest.capability_count;
  for (size_t index = 0; index < registered.capability_count; ++index) {
    const ApplicationCapabilityRequirement &requirement =
        application.manifest.capabilities[index];
    if (requirement.id == nullptr || requirement.id[0] == '\0') {
      registered = {};
      error = "invalid_application_capability";
      return false;
    }
    registered.capabilities[index].id = requirement.id;
    registered.capabilities[index].required = requirement.required;
  }

  registered.service_count = application.manifest.service_count;
  for (size_t index = 0; index < registered.service_count; ++index) {
    const ApplicationServiceRequirement &requirement =
        application.manifest.services[index];
    if (requirement.id == nullptr || requirement.id[0] == '\0') {
      registered = {};
      error = "invalid_application_service";
      return false;
    }
    for (size_t previous = 0; previous < index; ++previous) {
      if (registered.services[previous].id == requirement.id) {
        registered = {};
        error = "duplicate_application_service";
        return false;
      }
    }
    registered.services[index].id = requirement.id;
    registered.services[index].required = requirement.required;
  }

  registered.resource_count = application.manifest.resource_count;
  for (size_t index = 0; index < registered.resource_count; ++index) {
    const lunaros::ResourceRequirement &requirement =
        application.manifest.resources[index];
    if (requirement.id == nullptr || requirement.id[0] == '\0') {
      registered = {};
      error = "invalid_application_resource";
      return false;
    }
    for (size_t previous = 0; previous < index; ++previous) {
      if (registered.resources[previous].id == requirement.id) {
        registered = {};
        error = "duplicate_application_resource";
        return false;
      }
    }
    registered.resources[index].id = requirement.id;
    registered.resources[index].access = requirement.access;
  }

  registered.context = application.context;
  registered.prepare = application.prepare;
  registered.start = application.start;
  registered.start_with_reason = application.start_with_reason;
  registered.tick = application.tick;
  registered.stop = application.stop;
  if (!declare_capabilities_atomically(application.declare_capabilities, error)) {
    registered = {};
    return false;
  }
  ++application_count_;
  return true;
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

  setup_started_ = true;
  if (active_application_ != nullptr || pending_application_ != nullptr) {
    return true;
  }

  if (!switch_to(initial_application_id, error)) {
    state_ = AppControllerState::Error;
    last_error_ = error;
    return false;
  }
  return true;
}

void AppController::tick() {
  if (pending_application_ != nullptr) {
    advance_transition();
  }
  if (active_application_ != nullptr && active_application_->tick != nullptr) {
    active_application_->tick(active_application_->context);
  }
}

AppController::RegisteredApplication *
AppController::find_application(const std::string &application_id) {
  for (size_t index = 0; index < application_count_; ++index) {
    RegisteredApplication &application = applications_[index];
    if (application_id == application.id) {
      return &application;
    }
  }
  return nullptr;
}

const AppController::RegisteredApplication *
AppController::find_application(const std::string &application_id) const {
  for (size_t index = 0; index < application_count_; ++index) {
    const RegisteredApplication &application = applications_[index];
    if (application_id == application.id) {
      return &application;
    }
  }
  return nullptr;
}

void AppController::copy_resource_requirements(
    const RegisteredApplication &application,
    std::array<lunaros::ResourceRequirement, kApplicationResourceCapacity>
        &requirements) const {
  for (size_t index = 0; index < application.resource_count; ++index) {
    requirements[index].id = application.resources[index].id.c_str();
    requirements[index].access = application.resources[index].access;
  }
}

bool AppController::application_available(
    const RegisteredApplication &application, std::string &reason) const {
  for (size_t index = 0; index < application.capability_count; ++index) {
    const auto &requirement = application.capabilities[index];
    if (requirement.required &&
        !lunaredu::lunaros::device_capability_active(
            requirement.id.c_str())) {
      reason = "missing_capability:" + requirement.id;
      return false;
    }
  }

  for (size_t index = 0; index < application.service_count; ++index) {
    const auto &requirement = application.services[index];
    if (requirement.required &&
        !lunaros::service_available(requirement.id.c_str())) {
      reason = "missing_service:" + requirement.id;
      return false;
    }
  }

  if (&application == active_application_ ||
      &application == pending_application_) {
    reason.clear();
    return true;
  }

  std::array<lunaros::ResourceRequirement, kApplicationResourceCapacity>
      requirements;
  copy_resource_requirements(application, requirements);
  const char *replacing_owner =
      transition_phase_ == AppTransitionPhase::Idle &&
              active_application_ != nullptr
          ? active_application_->resource_owner_id.c_str()
          : nullptr;
  return resource_registry_.can_reserve(
      application.resource_owner_id, requirements.data(),
      application.resource_count, replacing_owner, reason);
}

} // namespace lunaredu::app
