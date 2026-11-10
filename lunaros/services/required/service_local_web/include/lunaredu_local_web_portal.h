#pragma once
#include "lunaredu_local_web_config.h"
#include <cstddef>
#include <cstdint>
#include <string>

namespace lunaredu::lunaros {
// Call once before setup_device_api. With no call, all routes use the built-in
// portal. Missing overrides fall back page-by-page; /recovery always bypasses
// overrides. Invalid configuration leaves the previous configuration intact.
bool configure_device_web(const DeviceWebConfig &config, std::string &error);
bool device_web_local_operation_allowed(const char *operation);
bool device_web_operation_permitted(const char *operation);
bool device_web_bound_static_page_allowed(const char *route);
bool device_web_bound_factory_page_allowed(const char *route);
bool device_web_bound_factory_operation_allowed(const char *path, bool post);
} // namespace lunaredu::lunaros
