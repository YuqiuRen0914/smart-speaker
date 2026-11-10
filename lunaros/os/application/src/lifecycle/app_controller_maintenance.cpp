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

bool AppController::before_service_restart(const char *service_id,
                                            std::string &error) {
  if (transition_.phase != AppTransitionPhase::Idle) {
    error = "application_switch_in_progress";
    return false;
  }
  if (!maintenance_restore_.app_id.empty()) {
    if (maintenance_restore_.service_id == service_id) return true;
    error = "application_service_restore_pending";
    return false;
  }
  maintenance_restore_.clear();
  if (active_application_ == nullptr) return true;
  bool requires_service = false;
  for (size_t index = 0; index < active_application_->service_count; ++index) {
    const auto &requirement = active_application_->services[index];
    if (requirement.required && requirement.id == service_id) {
      requires_service = true;
      break;
    }
  }
  if (!requires_service) return true;
  const std::string restore_id = active_application_->id;
  if (!stop("service_maintenance_restart")) { error = last_error_; return false; }
  maintenance_restore_.app_id = restore_id;
  maintenance_restore_.service_id = service_id;
  return true;
}

bool AppController::after_service_restart(const char *service_id,
                                           bool restarted,
                                           std::string &error) {
  if (maintenance_restore_.app_id.empty() ||
      maintenance_restore_.service_id != service_id) return true;
  if (!device_.runtime.service_available(service_id)) {
    if (!restarted) {
      maintenance_restore_.clear();
    }
    return true;
  }
  const std::string restore_id = maintenance_restore_.app_id;
  maintenance_restore_.clear();
  std::string restore_error;
  if (!switch_to(restore_id, ApplicationEnterReason::User, restore_error)) {
    if (!error.empty()) error += ";";
    error += "application_restore_failed:" + restore_error;
    return false;
  }
  return true;
}

} // namespace lunaredu::app
