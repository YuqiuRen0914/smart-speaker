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

bool AppController::reset_composition(std::string &error) {
  if (setup_started_) { error = "application_composition_sealed"; return false; }
  if (!device_.runtime.can_reset_composition(error)) return false;
  reset_declarations();
  error.clear(); return true;
}

void AppController::reset_declarations() {
  if (setup_started_) std::abort(); // Firmware composition is boot-only.
  maintenance_restore_.clear();
  transition_={};
  launches_={};
  capability_ticks_={};capability_tick_count_=0;
  device_.model.capabilities.deactivate(kApplicationCapability.id);
  for (auto &application : applications_) {
    if (!application.id.empty())
      (void)device_.runtime.forget_task_owner(lunaros::ManagedTaskOwnerKind::Application, application.id.c_str());
    for(size_t s=0;s<application.service_count;++s){std::string error;(void)device_.runtime.release_service_use(application.resource_owner_id.c_str(),application.services[s].token,error);}
    for(size_t s=0;s<application.execution_resource_count;++s){std::string error;(void)device_.runtime.release_service_resource(application.execution_resources[s].window,error);}
    if(application.execution_lease.generation)
      (void)device_.runtime.execution_admission().release(application.resource_owner_id.c_str(),application.execution_lease);
    if(application.capability_execution_lease.generation)
      (void)device_.runtime.execution_admission().release(application.resource_owner_id.c_str(),application.capability_execution_lease);
    application = {};
  }
  application_count_ = 0;
  capabilities_.reset_capabilities();
}

void *AppController::foreground_context(const char *id) const {
  if (id) for (size_t i = 0; i < application_count_; ++i)
    if (applications_[i].id == id) return applications_[i].context;
  return nullptr;
}

bool AppController::register_foreground(
    const ForegroundDefinition &application, std::string &error) {
  error.clear();
  if (setup_started_ || capabilities_.capabilities_sealed()) {
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
  if (application.manifest.is_home) {
    for (size_t i = 0; i < application_count_; ++i) {
      if (applications_[i].is_home) {
        error = "application_home_already_registered";
        return false;
      }
    }
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
  if(application.manifest.execution_resource_count>kApplicationResourceCapacity ||
     (application.manifest.execution_resource_count&&!application.manifest.execution_resources)){
    error="invalid_application_execution_resources";return false;
  }
  if (application.manifest.resource_count > 0 &&
      application.manifest.resources == nullptr) {
    error = "invalid_application_resources";
    return false;
  }
  if (application.manifest.settings_route != nullptr &&
      application.manifest.settings_route[0] != '\0' &&
      application.manifest.kind != ForegroundKind::Application) {
    error = "settings_route_requires_application";
    return false;
  }
  if (application.manifest.settings_route != nullptr &&
      std::strlen(application.manifest.settings_route) >= 48) {
    error = "settings_route_too_long";
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
  registered.settings_route = application.manifest.settings_route != nullptr
                                  ? application.manifest.settings_route
                                  : "";
  registered.kind = application.manifest.kind;
  registered.backend = application.backend;
  registered.system_ui = application.manifest.system_ui;
  registered.is_home = application.manifest.is_home;
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
    registered.services[index].use = requirement.use;
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
  registered.execution_resource_count=application.manifest.execution_resource_count;
  for(size_t i=0;i<registered.execution_resource_count;++i){
    const auto *id=application.manifest.execution_resources[i];
    if(!id||!*id||std::strlen(id)>64){registered={};error="invalid_application_execution_resource";return false;}
    for(size_t j=0;j<i;++j)if(registered.execution_resources[j].id==id){registered={};error="duplicate_application_execution_resource";return false;}
    registered.execution_resources[i].id=id;
  }
  registered.tick = application.tick;
  registered.capability_tick = application.capability_tick;
  registered.stop = application.stop;
  registered.stop_checked = application.stop_checked;
  registered.preparation = application.preparation;
  registered.startup_timeout_ms=application.startup_timeout_ms?application.startup_timeout_ms:30000;
  registered.execution=application.execution;
  if (!device_.runtime.declare_task_owner(lunaros::ManagedTaskOwnerKind::Application,
                                        application.id, error)) { registered = {}; return false; }
  if(application.capability_tick) {
    const char *execution_error=nullptr;
    if(!device_.runtime.execution_admission().reserve(registered.resource_owner_id.c_str(),
        device_.runtime.current_task_execution(application.capability_execution),registered.capability_execution_lease,execution_error)) {
      error=execution_error?execution_error:"application_execution_not_admitted";
      (void)device_.runtime.forget_task_owner(lunaros::ManagedTaskOwnerKind::Application,application.id);
      registered={};return false;
    }
  }
  if (!capabilities_.declare_atomically(application.declare_capabilities, error)) {
    if(registered.capability_execution_lease.generation)
      (void)device_.runtime.execution_admission().release(registered.resource_owner_id.c_str(),registered.capability_execution_lease);
    (void)device_.runtime.forget_task_owner(lunaros::ManagedTaskOwnerKind::Application, application.id);
    registered = {};
    return false;
  }
  if(registered.capability_tick)capability_ticks_[capability_tick_count_++]=&registered;
  ++application_count_;
  return true;
}

AppController::RegisteredApplication *
AppController::find_application(std::string_view application_id) {
  for (size_t index = 0; index < application_count_; ++index) {
    RegisteredApplication &application = applications_[index];
    if (application_id == application.id) {
      return &application;
    }
  }
  return nullptr;
}

const AppController::RegisteredApplication *
AppController::find_application(std::string_view application_id) const {
  for (size_t index = 0; index < application_count_; ++index) {
    const RegisteredApplication &application = applications_[index];
    if (application_id == application.id) {
      return &application;
    }
  }
  return nullptr;
}

} // namespace lunaredu::app
