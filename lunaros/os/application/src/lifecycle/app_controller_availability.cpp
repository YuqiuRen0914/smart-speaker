#include "app/execution/application_context.h"
#include "kernel/runtime.h"
#include <cstdlib>
#include <cstring>
#include <new>
#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#endif
#include "app/lifecycle/app_controller.h"
#include "app/lifecycle/app_commands.h"

#include "model/device_model.h"


namespace lunaredu::app {

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
  const uint32_t capability_revision =
      device_.model.capabilities.revision();
  const uint32_t service_revision = device_.runtime.service_revision();
  if (application.availability_cache_valid &&
      application.availability_capability_revision == capability_revision &&
      application.availability_service_revision == service_revision &&
      application.availability_resource_revision == resource_revision_) {
    reason = application.availability_cached_reason;
    return application.availability_cached_value;
  }

  const auto cache_result = [&](bool available, const std::string &why) {
    application.availability_cache_valid = true;
    application.availability_capability_revision = capability_revision;
    application.availability_service_revision = service_revision;
    application.availability_resource_revision = resource_revision_;
    application.availability_cached_value = available;
    application.availability_cached_reason = why;
    reason = why;
    return available;
  };

  for (size_t index = 0; index < application.capability_count; ++index) {
    const auto &requirement = application.capabilities[index];
    if (requirement.required &&
        !device_.model.capabilities.active(
            requirement.id.c_str())) {
      return cache_result(false, "missing_capability:" + requirement.id);
    }
  }

  for (size_t index = 0; index < application.service_count; ++index) {
    const auto &requirement = application.services[index];
    if (requirement.required &&
        !device_.runtime.service_available(requirement.id.c_str())) {
      return cache_result(false, "missing_service:" + requirement.id);
    }
  }

  if (&application == active_application_ ||
      &application == transition_.target) {
    return cache_result(true, std::string());
  }

  std::array<lunaros::ResourceRequirement, kApplicationResourceCapacity>
      requirements;
  copy_resource_requirements(application, requirements);
  const char *replacing_owner =
      transition_.phase == AppTransitionPhase::Idle &&
              active_application_ != nullptr
          ? active_application_->resource_owner_id.c_str()
          : nullptr;
  const bool available = resource_registry_.can_reserve(
      application.resource_owner_id, requirements.data(),
      application.resource_count, replacing_owner, reason);
  return cache_result(available, reason);
}

} // namespace lunaredu::app
