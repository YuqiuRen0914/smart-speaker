#include "lunaros_local_web_application.h"
#include "device_api/routes/application_routes.h"
#include "device_api/http/embedded_http_server.h"
#include "device_api/http/http_request_api.h"
#include "device_api/access/device_api_request_guard.h"
#include "device_api/access/device_api_bound_access.h"
#include "lunaros_network.h"
#include <cstring>
namespace {
std::span<const lunaredu::lunaros::LocalWebApplicationRoute> routes;
}
namespace lunaredu::lunaros {
bool configure_local_web_application(std::span<const LocalWebApplicationRoute> definitions, std::string &error) {
  if (!routes.empty() || embedded_http_server_handle() || definitions.empty() || definitions.size() > 8) {
    error = "application_routes_already_configured_or_invalid"; return false;
  }
  for (size_t i = 0; i < definitions.size(); ++i) {
    const auto &r = definitions[i];
    if (!r.path || std::strncmp(r.path, "/app-api/", 9) ||
        !r.operation || !*r.operation || !r.handle || std::strlen(r.path) > 96 ||
        std::strpbrk(r.path, "*?#")) { error = "invalid_application_route"; return false; }
    for (size_t j = 0; j < i; ++j)
      if (r.post == definitions[j].post && !std::strcmp(r.path, definitions[j].path)) {
        error = "duplicate_application_route"; return false;
      }
  }
  routes = definitions; error.clear(); return true;
}
}
void register_local_application_routes() {
  using namespace lunaredu::lunaros;
  for (const auto &route : routes) {
    const auto *r = &route;
    register_http_route(r->path, r->post ? HTTP_POST : HTTP_GET, [r] {
      add_response_header("Cache-Control", "no-store");
      const auto origin = request_header("Origin");
      const auto net = network_snapshot();
      if (!device_api_origin_allowed(origin.c_str(), net.local_ip.c_str()) ||
          !device_api_current_request_allowed(DeviceApiAccessSurface::Operation, r->operation)) {
        send_json_response(403, "{\"error\":\"operation_not_permitted\"}"); return;
      }
      const auto grant = request_header("X-Lunar-Access-Grant");
      if (device_api_bound_access_required() && !device_api_bound_access_authorized(grant.c_str())) {
        send_json_response(403, "{\"error\":\"access_required\"}"); return;
      }
      if (request_content_length() > 512 || request_body_size() != request_content_length()) {
        send_json_response(413, "{\"error\":\"request_too_large\"}"); return;
      }
      LocalWebApplicationRequest request{request_body_data(), request_body_size(),
          current_request_context_handle(), [](void *p, const char *name) { return request_context_arg(name, p); }};
      LocalWebApplicationResponse response;
      r->handle(r->context, request, response);
      // A binary loan is released after synchronous send even if the client left.
      struct Release { LocalWebApplicationResponse &r; ~Release() { if (r.complete) r.complete(r.loan); } } release{response};
      if (device_api_bound_access_required() && !device_api_bound_access_authorized(grant.c_str())) {
        send_json_response(403, "{\"error\":\"authority_changed\"}"); return;
      }
      if (response.data) (void)send_http_binary_response(response.status, response.content_type, response.data, response.bytes);
      else send_http_response(response.status, response.content_type, response.body);
    });
  }
}
