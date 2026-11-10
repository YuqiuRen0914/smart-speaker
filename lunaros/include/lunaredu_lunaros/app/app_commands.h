#pragma once

#include <string>

#include "lunaredu_lunaros/app/app_controller.h"

namespace lunaredu::app {

// Binds the product-owned controller to the shared typed command facade.
// The controller must outlive the LunarOS runtime.
bool bind_app_controller(AppController &controller, std::string &error);

// Safe sibling entrypoints for Device API, DeviceLink and other transports.
bool request_application_status(AppControllerStatusSnapshot &snapshot,
                                std::string &error);
bool request_application_snapshot(size_t index, ApplicationSnapshot &snapshot,
                                  std::string &error);
bool request_application_switch(const std::string &application_id,
                                AppControllerStatusSnapshot &snapshot,
                                std::string &error);
bool request_application_stop(const std::string &reason,
                              AppControllerStatusSnapshot &snapshot,
                              std::string &error);

} // namespace lunaredu::app
