#include "lunaredu_local_web_portal.h"
namespace lunaredu::lunaros {
bool device_web_local_operation_allowed(const char *) { return false; }
bool device_web_operation_permitted(const char *) { return true; }
bool device_web_bound_static_page_allowed(const char *) { return false; }
bool device_web_bound_factory_page_allowed(const char *) { return false; }
bool device_web_bound_factory_operation_allowed(const char *, bool) { return false; }
bool configure_device_web(const DeviceWebConfig &, std::string &error) {
  error = "device_web_disabled";
  return false;
}
}
