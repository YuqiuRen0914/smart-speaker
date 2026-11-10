#include "lunaredu_lunaros/network/network.h"
#include "device_api/http/http_request_api.h"

#include <cstdio>
#include <string>
#include <vector>

#include <ArduinoJson.h>
#include <esp_err.h>
#include <esp_netif.h>
#include <lwip/inet.h>
#include <lwip/sockets.h>

#include "device_api/http/http_request_context.h"
#include "device_api/http/http_status.h"
#include "device_api/access/device_api_access_policy.h"

namespace {

void set_response_headers(HttpRequestContext &context, int status_code,
                          const char *content_type) {
  httpd_resp_set_status(
      context.request, lunaredu::lunaros::internal::http_status_text(status_code));
  if (content_type != nullptr) {
    httpd_resp_set_type(context.request, content_type);
  }
  // The workbench polls several modules in parallel. Keeping every browser
  // socket alive exhausts the ESP HTTP server's small session pool and makes
  // later requests fail with "Failed to fetch" even though Wi-Fi is healthy.
  // Device API responses are small and infrequent enough that deterministic
  // connection release is preferable to HTTP keep-alive here.
  httpd_resp_set_hdr(context.request, "Connection", "close");
  for (const auto &header : context.pending_headers) {
    httpd_resp_set_hdr(context.request, header.first.c_str(),
                       header.second.c_str());
  }
}

std::string arg_value_from(const HttpRequestContext &context, const char *name) {
  if (name == nullptr) {
    return std::string();
  }

  for (auto iterator = context.args.rbegin(); iterator != context.args.rend();
       ++iterator) {
    if (iterator->first == name) {
      return iterator->second;
    }
  }
  return std::string();
}

bool arg_exists_in(const HttpRequestContext &context, const char *name) {
  if (name == nullptr) {
    return false;
  }

  for (auto iterator = context.args.rbegin(); iterator != context.args.rend();
       ++iterator) {
    if (iterator->first == name) {
      return true;
    }
  }
  return false;
}

std::string context_header_value(const HttpRequestContext &context,
                                 const char *name) {
  return header_value(context.request, name);
}

bool add_cors_headers(HttpRequestContext &context) {
  const std::string origin = context_header_value(context, "Origin");
  if (!device_api_origin_allowed(origin.c_str(), lunaredu::lunaros::network_snapshot().local_ip.c_str())) {
    return false;
  }
  if (!origin.empty()) {
    context.pending_headers.emplace_back("Access-Control-Allow-Origin",
                                         origin);
    context.pending_headers.emplace_back("Vary", "Origin");
  }
  context.pending_headers.emplace_back("Access-Control-Allow-Methods",
                                       "GET,POST,PUT,DELETE,OPTIONS");
  context.pending_headers.emplace_back("Access-Control-Allow-Headers",
                                       "Content-Type");
  context.pending_headers.emplace_back("Access-Control-Max-Age", "600");
  if (context_header_value(context,
                           "Access-Control-Request-Private-Network") ==
      "true") {
    context.pending_headers.emplace_back(
        "Access-Control-Allow-Private-Network", "true");
  }
  return true;
}

} // namespace

thread_local HttpRequestContext *g_request_context = nullptr;

std::string header_value(httpd_req_t *request, const char *name) {
  if (request == nullptr || name == nullptr) return {};
  const size_t value_length = httpd_req_get_hdr_value_len(request, name);
  if (value_length == 0) return {};
  std::string value(value_length + 1, '\0');
  if (httpd_req_get_hdr_value_str(request, name, value.data(), value.size()) != ESP_OK) {
    return {};
  }
  value.resize(std::char_traits<char>::length(value.c_str()));
  return value;
}

std::string current_sta_ip_string() {
  esp_netif_ip_info_t ip_info = {};
  esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  if (netif == nullptr || esp_netif_get_ip_info(netif, &ip_info) != ESP_OK) {
    return std::string();
  }

  char buffer[20];
  std::snprintf(buffer, sizeof(buffer), IPSTR, IP2STR(&ip_info.ip));
  return std::string(buffer);
}

std::string current_softap_ip_string() {
  esp_netif_ip_info_t ip_info = {};
  esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
  if (netif == nullptr || esp_netif_get_ip_info(netif, &ip_info) != ESP_OK) {
    return std::string();
  }

  char buffer[20];
  std::snprintf(buffer, sizeof(buffer), IPSTR, IP2STR(&ip_info.ip));
  return std::string(buffer);
}

std::string current_request_peer_address() {
  if (g_request_context == nullptr || g_request_context->request == nullptr) {
    return {};
  }
  const int socket = httpd_req_to_sockfd(g_request_context->request);
  if (socket < 0) return {};
  sockaddr_storage address = {};
  socklen_t address_length = sizeof(address);
  if (getpeername(socket, reinterpret_cast<sockaddr *>(&address),
                  &address_length) != 0) {
    return {};
  }
  char buffer[INET6_ADDRSTRLEN] = {};
  const void *source = nullptr;
  int family = address.ss_family;
  if (family == AF_INET) {
    source = &reinterpret_cast<const sockaddr_in *>(&address)->sin_addr;
  } else if (family == AF_INET6) {
    source = &reinterpret_cast<const sockaddr_in6 *>(&address)->sin6_addr;
  } else {
    return {};
  }
  return inet_ntop(family, source, buffer, sizeof(buffer)) == nullptr
             ? std::string()
             : std::string(buffer);
}

void send_json_response(int status_code, const std::string &body) {
  send_http_response(status_code, "application/json; charset=utf-8", body);
}

void send_http_response(int status_code, const char *content_type,
                        const std::string &body) {
  if (g_request_context == nullptr) {
    return;
  }

  (void)add_cors_headers(*g_request_context);
  set_response_headers(*g_request_context, status_code, content_type);
  httpd_resp_send(g_request_context->request, body.c_str(), body.length());
}

void send_http_response(int status_code, const char *content_type,
                        const char *body) {
  send_http_response(status_code, content_type,
                     body ? std::string(body) : std::string());
}

bool send_http_binary_response(int status_code, const char *content_type,
                               const uint8_t *data, size_t size) {
  if (g_request_context == nullptr || data == nullptr) {
    return false;
  }

  (void)add_cors_headers(*g_request_context);
  set_response_headers(*g_request_context, status_code, content_type);
  return httpd_resp_send(g_request_context->request,
                         reinterpret_cast<const char *>(data),
                         size) == ESP_OK;
}

bool begin_http_stream(int status_code, const char *content_type) {
  if (g_request_context == nullptr) {
    return false;
  }

  (void)add_cors_headers(*g_request_context);
  set_response_headers(*g_request_context, status_code, content_type);
  return true;
}

bool send_http_chunk(const uint8_t *data, size_t size) {
  if (g_request_context == nullptr || data == nullptr || size == 0) {
    return false;
  }

  return httpd_resp_send_chunk(g_request_context->request,
                               reinterpret_cast<const char *>(data),
                               size) == ESP_OK;
}

void end_http_stream() {
  if (g_request_context == nullptr) {
    return;
  }

  httpd_resp_send_chunk(g_request_context->request, nullptr, 0);
}

void add_response_header(const char *name, const char *value) {
  if (g_request_context == nullptr || name == nullptr || value == nullptr) {
    return;
  }
  g_request_context->pending_headers.emplace_back(name, value);
}

void serialize_and_send_json(int status_code, JsonDocument &doc) {
  std::string body;
  serializeJson(doc, body);
  send_json_response(status_code, body);
}

void handle_options() {
  const std::string origin = request_header("Origin");
  if (!device_api_origin_allowed(origin.c_str(), lunaredu::lunaros::network_snapshot().local_ip.c_str())) {
    send_http_response(403, "text/plain", "origin_not_allowed");
    return;
  }
  send_http_response(204, "text/plain", "");
}

bool parse_json_body(JsonDocument &doc) {
  const uint8_t *body = request_body_data();
  const size_t body_size = request_body_size();
  if (body == nullptr || body_size == 0) {
    return false;
  }

  const DeserializationError error = deserializeJson(
      doc, reinterpret_cast<const char *>(body), body_size);
  return error == DeserializationError::Ok;
}

bool request_has_arg(const char *name) {
  return g_request_context != nullptr && arg_exists_in(*g_request_context, name);
}

std::string request_arg(const char *name) {
  return g_request_context == nullptr ? std::string()
                                      : arg_value_from(*g_request_context, name);
}

HTTPMethod current_request_method() {
  return g_request_context != nullptr ? g_request_context->method : HTTP_GET;
}

std::string current_request_uri() {
  return g_request_context == nullptr || g_request_context->request == nullptr
             ? std::string()
             : std::string(g_request_context->request->uri);
}

const uint8_t *request_body_data() {
  return g_request_context == nullptr || g_request_context->body.empty()
             ? nullptr
             : g_request_context->body.data();
}

size_t request_body_size() {
  return g_request_context == nullptr ? 0 : g_request_context->body.size();
}

size_t request_content_length() {
  return g_request_context == nullptr || g_request_context->request == nullptr
             ? 0
             : g_request_context->request->content_len;
}

int read_request_body_chunk(uint8_t *buffer, size_t max_size) {
  if (g_request_context == nullptr || g_request_context->request == nullptr ||
      buffer == nullptr || max_size == 0) {
    return 0;
  }
  return httpd_req_recv(g_request_context->request,
                        reinterpret_cast<char *>(buffer), max_size);
}

std::string request_header(const char *name) {
  return g_request_context == nullptr ? std::string()
                                      : header_value(g_request_context->request, name);
}

void *current_request_context_handle() { return g_request_context; }

bool request_context_has_arg(const char *name, void *context) {
  auto *request_context = static_cast<HttpRequestContext *>(context);
  return request_context != nullptr &&
         arg_exists_in(*request_context, name);
}

std::string request_context_arg(const char *name, void *context) {
  auto *request_context = static_cast<HttpRequestContext *>(context);
  return request_context == nullptr ? std::string()
                                    : arg_value_from(*request_context, name);
}

int read_request_body_chunk_from_context(uint8_t *buffer, size_t max_size,
                                         void *context) {
  auto *request_context = static_cast<HttpRequestContext *>(context);
  if (request_context == nullptr || request_context->request == nullptr ||
      buffer == nullptr || max_size == 0) {
    return 0;
  }
  return httpd_req_recv(request_context->request,
                        reinterpret_cast<char *>(buffer), max_size);
}

bool send_http_binary_response_from_context(
    int status_code, const char *content_type, const uint8_t *data,
    size_t size, void *context) {
  auto *request_context = static_cast<HttpRequestContext *>(context);
  if (request_context == nullptr || request_context->request == nullptr ||
      data == nullptr) {
    return false;
  }
  (void)add_cors_headers(*request_context);
  set_response_headers(*request_context, status_code, content_type);
  return httpd_resp_send(request_context->request,
                         reinterpret_cast<const char *>(data), size) == ESP_OK;
}

bool begin_http_stream_from_context(int status_code, const char *content_type,
                                    void *context) {
  auto *request_context = static_cast<HttpRequestContext *>(context);
  if (request_context == nullptr || request_context->request == nullptr) {
    return false;
  }
  (void)add_cors_headers(*request_context);
  set_response_headers(*request_context, status_code, content_type);
  return true;
}

bool send_http_chunk_from_context(const uint8_t *data, size_t size,
                                  void *context) {
  auto *request_context = static_cast<HttpRequestContext *>(context);
  if (request_context == nullptr || request_context->request == nullptr ||
      data == nullptr || size == 0) {
    return false;
  }
  return httpd_resp_send_chunk(request_context->request,
                               reinterpret_cast<const char *>(data),
                               size) == ESP_OK;
}

void end_http_stream_from_context(void *context) {
  auto *request_context = static_cast<HttpRequestContext *>(context);
  if (request_context != nullptr && request_context->request != nullptr) {
    httpd_resp_send_chunk(request_context->request, nullptr, 0);
  }
}
