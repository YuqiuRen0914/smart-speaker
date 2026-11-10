#include "lunaros_local_web_lifecycle.h"

#include "device_api/http/embedded_http_server.h"
#include "device_api/routes/device_api_routes.h"
#include "device_api/routes/rpc_routes.h"
#include "device_api/routes/application_routes.h"
#include "device_api/local_web_demand.h"
#include "device_api/access/device_api_bound_access.h"
#include "device_api/access/device_api_request_guard.h"
#include <atomic>

namespace lunaredu::lunaros {
namespace { std::atomic<bool> management_http_started{false}; }

bool local_web_start(DeviceProjectionRegistry &registry, std::string &error) {
  if (web_internal::demand_enabled()) return web_internal::demand_start(registry,error);
  return web_internal::start_full(registry,error);
}

bool web_internal::start_full(DeviceProjectionRegistry &registry, std::string &error) {
  error.clear();
  if (embedded_http_server_handle() != nullptr) {
    error = "device_api_already_started";
    return false;
  }
  management_http_started.store(false, std::memory_order_release);
  reset_device_http_routes();
  register_device_http_routes(registry);
  setup_device_web_routes(registry);
  register_local_rpc_route();
  register_local_application_routes();
  if (!start_embedded_http_server_if_needed()) {
    error = "device_api_http_start_failed";
    return false;
  }
  management_http_started.store(true, std::memory_order_release);
  return true;
}

bool local_web_stop(std::string &error) {
  if (web_internal::demand_enabled()) return web_internal::demand_stop(error);
  return web_internal::stop_full(error);
}
bool web_internal::stop_full(std::string &error) {
  management_http_started.store(false, std::memory_order_release);
  return stop_embedded_http_server(error);
}
bool local_web_management_ready() {
  return management_http_started.load(std::memory_order_acquire);
}

}  // namespace lunaredu::lunaros
