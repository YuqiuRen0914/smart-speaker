#pragma once

#include <string>

#include "app/lifecycle/app_controller.h"
#include "kernel/runtime.h"

namespace lunaredu::app {

inline constexpr lunaros::DeviceCapabilityDescriptor kApplicationCapability{"application_controller"};

// Commands resolve the device's typed owner and execute on main.
bool request_application_status(lunaros::ApplicationContext &device,
                                AppControllerStatusSnapshot &snapshot, std::string &error);
bool request_application_snapshot(lunaros::ApplicationContext &device, size_t index,
                                  ApplicationSnapshot &snapshot, std::string &error);
bool request_application_switch(lunaredu::lunaros::ApplicationContext &device,
                                const std::string &application_id,
                                AppControllerStatusSnapshot &snapshot,
                                std::string &error);
bool request_application_stop(lunaredu::lunaros::ApplicationContext &device,
                              const std::string &reason,
                              AppControllerStatusSnapshot &snapshot,
                              std::string &error);

} // namespace lunaredu::app
