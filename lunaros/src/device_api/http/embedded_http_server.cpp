#include "lunaredu_lunaros/network/network.h"
#include "device_api/http/embedded_http_server.h"

#include <cstdlib>
#include <cstring>
#include <list>
#include <string_view>
#include <utility>
#include <vector>

#include <esp_err.h>
#include <lwip/inet.h>
#include <lwip/sockets.h>

#include "lunaredu_lunaros/device_api/device_api.h"
#include "lunaredu_lunaros/kernel/log.h"
#include "sdkconfig.h"
#include "device_api/access/device_api_access_policy.h"
#include "device_api/routes/device_api_routes.h"
#include "device_api/http/http_request_api.h"
#include "device_api/http/http_request_context.h"
#include "device_api/http/embedded_http_server_internal.h"

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
  lunaredu::lunaros::report_device_host_connection(
      request_from_lan(request)
          ? lunaredu::lunaros::DeviceHostConnectionState::Lan
          : lunaredu::lunaros::DeviceHostConnectionState::Connected);
}

void report_host_error() {
  lunaredu::lunaros::report_device_host_connection(
      lunaredu::lunaros::DeviceHostConnectionState::Error);
}

bool reject_disallowed_origin(httpd_req_t *request) {
  const std::string origin = header_value(request, "Origin");
  if (device_api_origin_allowed(origin.c_str(), lunaredu::lunaros::network_snapshot().local_ip.c_str())) {
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
    std::vector<char> buffer(query_length + 1, '\0');
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

  HttpRequestContext context;
  context.request = request;
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
  (void)error;
  report_host_request(request);
  if (reject_disallowed_origin(request)) {
    return ESP_OK;
  }
  if (g_not_found_handler == nullptr) {
    httpd_resp_send_err(request, HTTPD_404_NOT_FOUND, "not_found");
    return ESP_OK;
  }

  HttpRequestContext context;
  context.request = request;
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
    LOGE("device_api_http", "注册路由失败 uri=%s method=%d err=%s",
         route.uri.c_str(), route.method, esp_err_to_name(result));
    return false;
  }
  LOGI("device_api_http", "route ready uri=%s method=%d",
       route.uri.c_str(), route.method);
  return true;
}

bool register_routes_with_server_internal() {
  bool ok = true;
  for (HttpRouteEntry &route : g_routes) {
    ok = register_route_with_server(route) && ok;
  }
  return ok;
}

void register_required_route(const char *uri, HTTPMethod method,
                             HttpSimpleHandler handler) {
  if (!register_http_route(uri, method, handler)) {
    LOGE("device_api_http", "登记必需路由失败 uri=%s method=%d",
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
    LOGE("device_api_http", "failed to register 404 handler err=%s",
         esp_err_to_name(result));
  }
}

size_t device_api_route_count() { return g_routes.size(); }

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

void register_device_http_routes() {
  register_device_api_routes();
  register_required_route("/ping*", HTTP_GET, handle_ping);
  register_required_route("/ping*", HTTP_OPTIONS, handle_options);
}
