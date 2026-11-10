#include "device_api/local_web_activity.h"
#include "lunaros_network.h"
#include "device_api/http/embedded_http_server.h"

#include <cstdlib>
#include <cstring>
#include <list>
#include <string_view>
#include <utility>
#include <vector>

#include <esp_err.h>
#include <esp_timer.h>
#include <lwip/inet.h>
#include <lwip/sockets.h>

#include "device_api/device_api.h"
#include "device_api/device_projection.h"
#include <esp_log.h>
#include "sdkconfig.h"
#include "device_api/access/device_api_access_policy.h"
#include "device_api/access/device_api_bound_access.h"
#include "device_api/access/device_api_request_guard.h"
#include "lunaros_platform_link.h"
#include "lunaredu_local_web_portal.h"
#include "diagnostics/session.h"
#include "device_api/routes/device_api_routes.h"
#include "device_api/http/http_request_api.h"
#include "device_api/http/http_request_context.h"
#include "device_api/http/http_workspace.h"
#include "device_api/http/embedded_http_server_internal.h"
#include "lunaros_local_web_lifecycle.h"
#include "lunaros_local_web_socket.h"
#include <array>

#ifndef CONFIG_LUNAREDU_DEVICE_API_HTTP_MAX_BUFFERED_BODY_BYTES
#define CONFIG_LUNAREDU_DEVICE_API_HTTP_MAX_BUFFERED_BODY_BYTES 65536
#endif
#ifndef CONFIG_LUNAREDU_DEVICE_API_HTTP_BODY_RECEIVE_TIMEOUTS
#define CONFIG_LUNAREDU_DEVICE_API_HTTP_BODY_RECEIVE_TIMEOUTS 3
#endif

namespace {

constexpr size_t kMaxBufferedRequestBodyBytes =
    CONFIG_LUNAREDU_DEVICE_API_HTTP_MAX_BUFFERED_BODY_BYTES;
constexpr uint8_t kMaxBodyReceiveTimeouts =
    CONFIG_LUNAREDU_DEVICE_API_HTTP_BODY_RECEIVE_TIMEOUTS;
httpd_handle_t g_http_server = nullptr;
HttpSimpleHandler g_not_found_handler = nullptr;
lunaredu::lunaros::DeviceProjectionRegistry *g_projection_registry = nullptr;
size_t g_socket_count = 0;
#if CONFIG_HTTPD_WS_SUPPORT && CONFIG_HTTPD_WS_PRE_HANDSHAKE_CB_SUPPORT
const lunaredu::lunaros::LocalWebSocketEndpoint *g_socket_endpoints = nullptr;
bool g_sockets_frozen = false;
#endif

struct HttpRouteEntry {
  std::string uri;
  HTTPMethod method = HTTP_GET;
  HttpSimpleHandler handler = nullptr;
  HttpBodyBufferPolicy body_policy = nullptr;
};

enum class BodyReadResult : uint8_t {
  Ok,
  TooLarge,
  ReceiveFailed,
};

bool private_ipv4(uint32_t address_network_order) {
  const uint32_t address = ntohl(address_network_order);
  const uint8_t first = static_cast<uint8_t>(address >> 24);
  const uint8_t second = static_cast<uint8_t>(address >> 16);
  return first == 10 || first == 127 ||
         (first == 169 && second == 254) ||
         (first == 172 && second >= 16 && second <= 31) ||
         (first == 192 && second == 168);
}

bool request_from_lan(httpd_req_t *request) {
  if (request == nullptr) {
    return false;
  }
  const int socket = httpd_req_to_sockfd(request);
  sockaddr_storage peer{};
  socklen_t peer_length = sizeof(peer);
  if (socket < 0 ||
      getpeername(socket, reinterpret_cast<sockaddr *>(&peer),
                  &peer_length) != 0) {
    return false;
  }
  if (peer.ss_family == AF_INET) {
    return private_ipv4(
        reinterpret_cast<const sockaddr_in *>(&peer)->sin_addr.s_addr);
  }
  if (peer.ss_family == AF_INET6) {
    const auto &address =
        reinterpret_cast<const sockaddr_in6 *>(&peer)->sin6_addr;
    bool ipv4_mapped = address.s6_addr[10] == 0xFFU &&
                       address.s6_addr[11] == 0xFFU;
    for (size_t index = 0; index < 10 && ipv4_mapped; ++index) {
      ipv4_mapped = address.s6_addr[index] == 0;
    }
    if (ipv4_mapped) {
      uint32_t mapped_address = 0;
      std::memcpy(&mapped_address, &address.s6_addr[12],
                  sizeof(mapped_address));
      return private_ipv4(mapped_address);
    }
    bool loopback = address.s6_addr[15] == 1;
    for (size_t index = 0; index < 15 && loopback; ++index) {
      loopback = address.s6_addr[index] == 0;
    }
    const bool link_local = address.s6_addr[0] == 0xFEU &&
                            (address.s6_addr[1] & 0xC0U) == 0x80U;
    const bool unique_local = (address.s6_addr[0] & 0xFEU) == 0xFCU;
    return loopback || link_local || unique_local;
  }
  return false;
}

void report_host_request(httpd_req_t *request) {
  report_http_host_connection(
      request_from_lan(request)
          ? lunaredu::lunaros::DeviceHostConnectionState::Lan
          : lunaredu::lunaros::DeviceHostConnectionState::Connected);
}

void report_host_error() {
  report_http_host_connection(
      lunaredu::lunaros::DeviceHostConnectionState::Error);
}

bool reject_disallowed_origin(httpd_req_t *request) {
  const std::string origin = header_value(request, "Origin");
  if (device_api_origin_allowed(origin.c_str(),
          lunaredu::lunaros::network_snapshot().local_ip.c_str(),
          lunaredu::lunaros::platform_link_binding_browser_origin().c_str())) {
    return false;
  }

  // CORS response headers are not an authorization boundary: a browser can
  // still send a "simple" mutating request and merely hide the response from
  // JavaScript. Reject the request before buffering its body or invoking any
  // route so an untrusted web origin cannot drive a device on the user's LAN.
  httpd_resp_set_status(request, "403 Forbidden");
  httpd_resp_set_type(request, "text/plain; charset=utf-8");
  httpd_resp_set_hdr(request, "Connection", "close");
  httpd_resp_send(request, "origin_not_allowed", HTTPD_RESP_USE_STRLEN);
  return true;
}

bool bound_diagnostic_operation_allowed(httpd_req_t *request,
                                        const HttpRouteEntry &route) {
  using lunaredu::lunaros::DeviceDiagnosticAccess;
  using lunaredu::lunaros::DeviceOperationMethod;
  namespace diagnostics = lunaredu::lunaros;
  if (!diagnostics::diagnostic_session_enabled() ||
      !request_from_lan(request) || device_api_provisioning_surface_active() ||
      g_projection_registry == nullptr) return false;
  constexpr std::string_view prefix = "/api/v1/modules/";
  std::string_view path(request->uri);
  path = path.substr(0, path.find('?'));
  if (path.substr(0, prefix.size()) != prefix) return false;
  path.remove_prefix(prefix.size());
  const size_t separator = path.find('/');
  if (separator == std::string_view::npos || separator == 0 ||
      separator + 1 >= path.size() ||
      path.find('/', separator + 1) != std::string_view::npos) return false;
  const std::string module(path.substr(0, separator));
  const std::string operation(path.substr(separator + 1));
  const auto *model = g_projection_registry->context().models.find_operation(
      module.c_str(), operation.c_str());
  const auto *projection = g_projection_registry->find_operation(
      module.c_str(), operation.c_str());
  if (model == nullptr || projection == nullptr) return false;
  DeviceOperationMethod method = DeviceOperationMethod::Get;
  switch (route.method) {
  case HTTP_POST: method = DeviceOperationMethod::Post; break;
  case HTTP_PUT: method = DeviceOperationMethod::Put; break;
  case HTTP_DELETE: method = DeviceOperationMethod::Delete; break;
  default: break;
  }
  if (projection->method != method) return false;
  if (model->diagnostic_access == DeviceDiagnosticAccess::Bootstrap)
    return true;
  if (model->diagnostic_access != DeviceDiagnosticAccess::Session)
    return false;
  const std::string token = header_value(request, "X-Lunar-Debug-Token");
  return diagnostics::diagnostic_session_token_valid(token.c_str());
}

bool reject_missing_bound_grant(httpd_req_t *request,
                                const HttpRouteEntry &route) {
  if (!device_api_bound_access_required() || route.method == HTTP_OPTIONS ||
      (route.method == HTTP_GET && route.uri == "/api/v1/device") ||
      route.uri == "/ping*") return false;
  if (route.method == HTTP_GET &&
      lunaredu::lunaros::device_web_bound_static_page_allowed(route.uri.c_str()))
    return false;
  if (request_from_lan(request) && !device_api_provisioning_surface_active()) {
    if (route.method == HTTP_GET &&
        lunaredu::lunaros::device_web_bound_factory_page_allowed(
            route.uri.c_str())) return false;
    if ((route.method == HTTP_GET || route.method == HTTP_POST) &&
        lunaredu::lunaros::device_web_bound_factory_operation_allowed(
            request->uri, route.method == HTTP_POST)) return false;
  }
  const std::string grant = header_value(request, "X-Lunar-Access-Grant");
  if (route.uri == "/mcp" && route.method == HTTP_POST) {
    const auto bearer = header_value(request, "Authorization");
    if (bearer.rfind("Bearer ", 0) == 0 && device_api_bound_access_authorized(bearer.c_str() + 7)) return false;
  }
  if (device_api_bound_access_authorized(grant.c_str())) return false;
  if (device_api_shared_access_authorized(grant.c_str()) &&
      ((route.method == HTTP_GET && (route.uri == "/api/v1/capabilities" || route.uri == "/api/v1/lunarlink/manifest")) ||
       route.uri == "/api/v1/modules/*")) return false;
  const std::string administrator = header_value(request, "X-Lunar-Debug-Token");
  if (device_api_admin_access_authorized(administrator.c_str())) return false;
  if (bound_diagnostic_operation_allowed(request, route)) return false;
  httpd_resp_set_status(request, "401 Unauthorized");
  httpd_resp_set_type(request, "application/json; charset=utf-8");
  httpd_resp_set_hdr(request, "Connection", "close");
  httpd_resp_send(request,
      "{\"apiVersion\":\"1\",\"ok\":false,\"result\":{},\"error\":{\"code\":\"bound_access_required\",\"message\":\"Bound device access grant required\"}}",
      HTTPD_RESP_USE_STRLEN);
  return true;
}

std::list<HttpRouteEntry> g_routes;

std::string url_decode(std::string_view input) {
  std::string output;
  output.reserve(input.length());

  for (size_t index = 0; index < input.length(); ++index) {
    const char current = input[index];
    if (current == '+') {
      output.push_back(' ');
      continue;
    }

    if (current == '%' && index + 2 < input.length()) {
      char buffer[3] = {input[index + 1], input[index + 2], '\0'};
      char *end = nullptr;
      const long value = std::strtol(buffer, &end, 16);
      if (end != nullptr && *end == '\0') {
        output.push_back(static_cast<char>(value));
        index += 2;
        continue;
      }
    }

    output.push_back(current);
  }

  return output;
}

void parse_encoded_pairs(std::string_view source,
                         std::vector<std::pair<std::string, std::string>> &target) {
  if (source.empty()) {
    return;
  }

  size_t cursor = 0;
  while (cursor < source.length()) {
    size_t pair_end = cursor;
    while (pair_end < source.length() && source[pair_end] != '&') {
      ++pair_end;
    }

    const std::string_view pair = source.substr(cursor, pair_end - cursor);
    const std::string_view::size_type separator = pair.find('=');
    const std::string key =
        separator == std::string_view::npos
            ? url_decode(pair)
            : url_decode(pair.substr(0, separator));
    const std::string value =
        separator == std::string_view::npos
            ? std::string()
            : url_decode(pair.substr(separator + 1));
    target.emplace_back(key, value);
    cursor = pair_end + 1;
  }
}

void populate_request_query_context(HttpRequestContext &context) {
  context.method = context.request->method;

  const size_t query_length = httpd_req_get_url_query_len(context.request);
  if (query_length > 0) {
    auto &buffer=context.query;buffer.assign(query_length + 1, '\0');
    if (httpd_req_get_url_query_str(context.request, buffer.data(),
                                    buffer.size()) == ESP_OK) {
      parse_encoded_pairs(std::string_view(buffer.data(), query_length),
                          context.args);
    }
  }
}

BodyReadResult populate_request_body_context(HttpRequestContext &context) {
  if (context.request->content_len <= 0) {
    return BodyReadResult::Ok;
  }

  if (context.request->content_len > kMaxBufferedRequestBodyBytes) {
    return BodyReadResult::TooLarge;
  }

  context.body.resize(context.request->content_len);
  size_t received = 0;
  uint8_t timeout_count = 0;
  while (received < context.body.size()) {
    const int chunk =
        httpd_req_recv(context.request,
                       reinterpret_cast<char *>(context.body.data()) + received,
                       context.body.size() - received);
    if (chunk == HTTPD_SOCK_ERR_TIMEOUT &&
        timeout_count < kMaxBodyReceiveTimeouts) {
      ++timeout_count;
      continue;
    }
    if (chunk <= 0) {
      context.body.clear();
      return BodyReadResult::ReceiveFailed;
    }
    received += static_cast<size_t>(chunk);
  }

  const std::string content_type = header_value(context.request, "Content-Type");
  if (content_type.rfind("application/x-www-form-urlencoded", 0) == 0) {
    parse_encoded_pairs(std::string_view(
                            reinterpret_cast<const char *>(context.body.data()),
                            context.body.size()),
                        context.args);
  }

  return BodyReadResult::Ok;
}

esp_err_t dispatch_request(httpd_req_t *request) {
  struct Activity { Activity(){lunaredu::lunaros::local_web_request_started();} ~Activity(){lunaredu::lunaros::local_web_request_finished();} } activity;
  auto *route = static_cast<HttpRouteEntry *>(request->user_ctx);
  if (route == nullptr) {
    return ESP_FAIL;
  }

  if (route->handler == nullptr) {
    return ESP_FAIL;
  }

  report_host_request(request);
  if (reject_disallowed_origin(request)) {
    return ESP_OK;
  }
  if (reject_missing_bound_grant(request, *route)) return ESP_OK;

  HttpContextLease workspace_lease;auto &context=workspace_lease.context();
  context.request = request;
  context.started_us = esp_timer_get_time();
  populate_request_query_context(context);
  g_request_context = &context;
  const bool should_buffer_body =
      route->body_policy == nullptr || route->body_policy();
  if (should_buffer_body) {
    const BodyReadResult body_result = populate_request_body_context(context);
    if (body_result == BodyReadResult::TooLarge) {
      g_request_context = nullptr;
      return httpd_resp_send_err(request, HTTPD_413_CONTENT_TOO_LARGE,
                                 "payload_too_large");
    }
    if (body_result == BodyReadResult::ReceiveFailed) {
      report_host_error();
      g_request_context = nullptr;
      return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST,
                                 "body_receive_failed");
    }
  }
  route->handler();
  g_request_context = nullptr;
  return ESP_OK;
}

esp_err_t dispatch_not_found(httpd_req_t *request, httpd_err_code_t error) {
  struct Activity { Activity(){lunaredu::lunaros::local_web_request_started();} ~Activity(){lunaredu::lunaros::local_web_request_finished();} } activity;
  (void)error;
  report_host_request(request);
  if (reject_disallowed_origin(request)) {
    return ESP_OK;
  }
  if (g_not_found_handler == nullptr) {
    httpd_resp_send_err(request, HTTPD_404_NOT_FOUND, "not_found");
    return ESP_OK;
  }

  HttpContextLease workspace_lease;auto &context=workspace_lease.context();
  context.request = request;
  context.started_us = esp_timer_get_time();
  populate_request_query_context(context);
  g_request_context = &context;
  const BodyReadResult body_result = populate_request_body_context(context);
  if (body_result == BodyReadResult::TooLarge) {
    g_request_context = nullptr;
    return httpd_resp_send_err(request, HTTPD_413_CONTENT_TOO_LARGE,
                               "payload_too_large");
  }
  if (body_result == BodyReadResult::ReceiveFailed) {
    report_host_error();
    g_request_context = nullptr;
    return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST,
                               "body_receive_failed");
  }
  g_not_found_handler();
  g_request_context = nullptr;
  return ESP_OK;
}

bool register_route_with_server(HttpRouteEntry &route) {
  if (g_http_server == nullptr || route.handler == nullptr) {
    return false;
  }

  httpd_uri_t uri = {};
  uri.uri = route.uri.c_str();
  uri.method = static_cast<httpd_method_t>(route.method);
  uri.handler = dispatch_request;
  uri.user_ctx = &route;
  const esp_err_t result = httpd_register_uri_handler(g_http_server, &uri);
  if (result != ESP_OK) {
    ESP_LOGE("device_api_http", "注册路由失败 uri=%s method=%d err=%s",
         route.uri.c_str(), route.method, esp_err_to_name(result));
    return false;
  }
  ESP_LOGI("device_api_http", "route ready uri=%s method=%d",
       route.uri.c_str(), route.method);
  return true;
}

bool register_routes_with_server_internal() {
  bool ok = true;
  for (HttpRouteEntry &route : g_routes) {
    ok = register_route_with_server(route) && ok;
  }
#if CONFIG_HTTPD_WS_SUPPORT && CONFIG_HTTPD_WS_PRE_HANDSHAKE_CB_SUPPORT
  g_sockets_frozen = true;
  for (size_t i = 0; i < g_socket_count; ++i) {
    const auto &endpoint = g_socket_endpoints[i];
    httpd_uri_t uri{}; uri.uri = endpoint.path; uri.method = HTTP_GET;
    uri.user_ctx = const_cast<lunaredu::lunaros::LocalWebSocketEndpoint *>(&endpoint);
    uri.is_websocket = true; uri.supported_subprotocol = endpoint.protocol;
    uri.ws_pre_handshake_cb = [](httpd_req_t *request) -> esp_err_t {
      if (reject_disallowed_origin(request)) return ESP_FAIL;
      const auto *endpoint = static_cast<const lunaredu::lunaros::LocalWebSocketEndpoint *>(request->user_ctx);
      const auto offered = header_value(request, "Sec-WebSocket-Protocol");
      if (device_api_provisioning_surface_active() || !endpoint ||
          !endpoint->authorize(endpoint->context, httpd_req_to_sockfd(request), offered.c_str())) {
        (void)httpd_resp_send_err(request, HTTPD_403_FORBIDDEN, "socket_authority_required"); return ESP_FAIL;
      }
      return ESP_OK;
    };
    uri.handler = [](httpd_req_t *request) -> esp_err_t {
      const auto *endpoint = static_cast<const lunaredu::lunaros::LocalWebSocketEndpoint *>(request->user_ctx);
      const int connection = httpd_req_to_sockfd(request);
      if (!endpoint || device_api_provisioning_surface_active() || !endpoint->authorize(endpoint->context, connection, nullptr)) return ESP_FAIL;
      if (request->method == HTTP_GET) return ESP_OK;
      httpd_ws_frame_t packet{};
      if (httpd_ws_recv_frame(request, &packet, 0) != ESP_OK || !packet.final || packet.len > 1024 ||
          (packet.type != HTTPD_WS_TYPE_TEXT && packet.type != HTTPD_WS_TYPE_BINARY)) return ESP_FAIL;
      std::array<uint8_t, 1024> bytes{}; packet.payload = bytes.data();
      if (httpd_ws_recv_frame(request, &packet, bytes.size()) != ESP_OK) return ESP_FAIL;
      const lunaredu::lunaros::LocalWebSocketFrame frame{connection, packet.type == HTTPD_WS_TYPE_BINARY,
        bytes.data(), packet.len, request,
        [](void *transport, bool binary, const uint8_t *data, size_t size) {
          httpd_ws_frame_t response{}; response.type = binary ? HTTPD_WS_TYPE_BINARY : HTTPD_WS_TYPE_TEXT;
          response.payload = const_cast<uint8_t *>(data); response.len = size;
          return httpd_ws_send_frame(static_cast<httpd_req_t *>(transport), &response) == ESP_OK;
        }};
      return endpoint->receive(endpoint->context, frame) ? ESP_OK : ESP_FAIL;
    };
    ok = httpd_register_uri_handler(g_http_server, &uri) == ESP_OK && ok;
  }
#endif
  return ok;
}

void register_required_route(const char *uri, HTTPMethod method,
                             HttpSimpleHandler handler) {
  if (!register_http_route(uri, method, handler)) {
    ESP_LOGE("device_api_http", "登记必需路由失败 uri=%s method=%d",
         uri ? uri : "(null)", method);
  }
}

void handle_ping() {
  send_json_response(200, "{\"ok\":true}");
}

} // 匿名命名空间

httpd_handle_t &device_api_http_server() { return g_http_server; }

bool register_device_api_routes_with_server() {
  return register_routes_with_server_internal();
}

void install_device_api_not_found_handler() {
  if (g_http_server == nullptr || g_not_found_handler == nullptr) {
    return;
  }
  const esp_err_t result = httpd_register_err_handler(
      g_http_server, HTTPD_404_NOT_FOUND, dispatch_not_found);
  if (result != ESP_OK) {
    ESP_LOGE("device_api_http", "failed to register 404 handler err=%s",
         esp_err_to_name(result));
  }
}

size_t device_api_route_count() { return g_routes.size() + g_socket_count; }

void reset_device_http_routes() {
  if (g_http_server != nullptr) {
    ESP_LOGW("device_api_http", "忽略运行中的 HTTP 路由重置");
    return;
  }
  g_routes.clear();
  g_not_found_handler = nullptr;
  g_projection_registry = nullptr;
}

bool register_http_route(const char *uri, HTTPMethod method,
                         HttpSimpleHandler handler) {
  return register_http_route_with_body_policy(uri, method, handler, nullptr);
}

bool register_http_route_with_body_policy(const char *uri, HTTPMethod method,
                                          HttpSimpleHandler handler,
                                          HttpBodyBufferPolicy body_policy) {
  if (uri == nullptr || handler == nullptr) {
    return false;
  }

  for (HttpRouteEntry &route : g_routes) {
    if (route.uri == uri && route.method == method) {
      route.handler = handler;
      route.body_policy = body_policy;
      return true;
    }
  }

  HttpRouteEntry entry;
  entry.uri = std::string(uri);
  entry.method = method;
  entry.handler = handler;
  entry.body_policy = body_policy;
  g_routes.push_back(std::move(entry));
  HttpRouteEntry &route = g_routes.back();
  if (g_http_server != nullptr && !register_route_with_server(route)) {
    g_routes.pop_back();
    return false;
  }
  return true;
}

void register_http_not_found_handler(HttpSimpleHandler handler) {
  g_not_found_handler = handler;
  install_device_api_not_found_handler();
}

void register_device_http_routes(lunaredu::lunaros::DeviceProjectionRegistry &registry) {
  g_projection_registry = &registry;
  register_device_model_routes(registry);
  register_device_operation_routes(registry);
  register_http_route("/api/v1/*", HTTP_OPTIONS, handle_options);
  register_required_route("/ping*", HTTP_GET, handle_ping);
  register_required_route("/ping*", HTTP_OPTIONS, handle_options);
}

bool lunaredu::lunaros::configure_local_web_sockets(const LocalWebSocketEndpoint *endpoints, size_t count, std::string &error) {
#if CONFIG_HTTPD_WS_SUPPORT && CONFIG_HTTPD_WS_PRE_HANDSHAKE_CB_SUPPORT
  if (g_sockets_frozen || g_http_server || (count && !endpoints)) { error = "web_socket_configuration_invalid"; return false; }
  for (size_t i = 0; i < count; ++i) {
    const auto &endpoint = endpoints[i];
    if (!endpoint.path || std::string_view(endpoint.path).substr(0, 12) != "/api/v1/app/" ||
        !endpoint.protocol || !endpoint.authorize || !endpoint.receive) { error = "web_socket_endpoint_invalid"; return false; }
    for (size_t j = 0; j < i; ++j) if (!std::strcmp(endpoint.path, endpoints[j].path)) { error = "web_socket_endpoint_duplicate"; return false; }
  }
  g_socket_endpoints = endpoints; g_socket_count = count; error.clear(); return true;
#else
  (void)endpoints; (void)count; error = "web_socket_not_enabled"; return false;
#endif
}

void report_http_host_connection(lunaredu::lunaros::DeviceHostConnectionState state) {
  if (g_projection_registry) lunaredu::lunaros::report_device_host_connection(g_projection_registry->device(), lunaredu::lunaros::DeviceHostTransport::LocalWeb, state);
}
