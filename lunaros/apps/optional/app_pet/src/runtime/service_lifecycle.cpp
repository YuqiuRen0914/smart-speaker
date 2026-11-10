#include "lunaredu_pet_service.h"
#include "lunaredu_pet_device_projection.h"

namespace lunaredu::pet {
bool stop_runtime_service(lunaros::DeviceRuntimeContext &model, std::string &error) {
  set_runtime_active(false);
  return set_pet_device_projection_available(model, false, error);
}
bool start_runtime_service(lunaros::DeviceRuntimeContext &model,
    const BehaviorPolicy &policy, std::string &error) {
  configure_behavior_policy(policy);
  error.clear();
  if (!setup_runtime()) error = "pet_runtime_initialization_failed";
  else {
    set_runtime_active(true);
    if (!set_pet_device_projection_available(model, true, error))
      error = "pet_projection_activation_failed:" + error;
  }
  if (error.empty()) return true;
  std::string ignored;
  (void)stop_runtime_service(model, ignored);
  return false;
}

}
