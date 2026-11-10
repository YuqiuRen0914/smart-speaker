#include "lunaros_local_web_rpc.h"
#include "device_api/routes/rpc_routes.h"
#include "device_api/http/http_request_api.h"
#include "device_api/http/embedded_http_server.h"
#include "device_api/access/device_api_bound_access.h"
#include "lunaros_platform_link.h"
#include <cstring>
namespace {
void *rpc_context = nullptr;
int (*rpc_invoke)(void *, const lunaredu::lunaros::LocalRpcRequest &, std::string &) = nullptr;
}
namespace lunaredu::lunaros {
bool configure_local_rpc(void *context, int (*invoke)(void *, const LocalRpcRequest &, std::string &), std::string &error) {
  if (!invoke || rpc_invoke || embedded_http_server_handle()) {error = "rpc_already_configured_or_started"; return false;}
  rpc_context = context; rpc_invoke = invoke; error.clear(); return true;
}
}
void register_local_rpc_route() {
  if (!rpc_invoke) return;
  const auto unsupported = [] {
    add_response_header("Cache-Control", "no-store");
    const auto origin=request_header("Origin");
    if (device_api_bound_access_required() && !origin.empty() && origin != lunaredu::lunaros::platform_link_binding_browser_origin()) {
      send_json_response(403, "{\"error\":\"invalid_origin\"}"); return;
    }
    add_response_header("Allow", "POST");
    send_json_response(405, "{\"error\":\"method_not_allowed\"}");
  };
  register_http_route("/mcp", HTTP_GET, unsupported);
  register_http_route("/mcp", HTTP_DELETE, unsupported);
  register_http_route("/mcp", HTTP_POST, [] {
    add_response_header("Cache-Control", "no-store");
    auto authorization = request_header("Authorization");
    const auto origin = request_header("Origin");
    // A browser origin must be the paired Board; native clients omit Origin.
    if (device_api_bound_access_required() && !origin.empty() && origin != lunaredu::lunaros::platform_link_binding_browser_origin()) {
      send_json_response(403, "{\"error\":\"invalid_origin\"}"); return;
    }
    if (device_api_bound_access_required() && (authorization.rfind("Bearer ", 0) != 0 ||
        !device_api_bound_access_authorized(authorization.c_str() + 7))) {
      send_json_response(401, "{\"error\":\"unauthorized\"}"); return;
    }
    if (request_content_length() > 8192 || request_body_size() != request_content_length()) {
      send_json_response(413, "{\"error\":\"request_too_large\"}"); return;
    }
    lunaredu::lunaros::LocalRpcRequest request{
      reinterpret_cast<const char *>(request_body_data()), request_body_size(),
      request_header("MCP-Protocol-Version"), request_header("Mcp-Method"), request_header("Mcp-Name")};
    request.document_allocator=current_http_json_allocator();request.schema_workspace=current_http_schema_workspace();
    request.read_header = [](void *, const char *name) { return request_header(name); };
    request.authority_context = &authorization;
    request.authorized = [](void *context) {
      const auto &token=*static_cast<const std::string *>(context);
      return !device_api_bound_access_required() || (token.rfind("Bearer ",0)==0 && device_api_bound_access_authorized(token.c_str()+7));
    };
    std::string local_response;auto *scratch=current_http_response_workspace();
    auto &response=scratch?*scratch:local_response;response.clear();
    const int status = rpc_invoke(rpc_context, request, response);
    // Reverify the short-lived grant after effects and before private results.
    if (device_api_bound_access_required() && !device_api_bound_access_authorized(authorization.c_str() + 7)) {
      send_json_response(403, "{\"error\":\"authority_changed\"}"); return;
    }
    send_http_response(status, "application/json", response);
  });
}
