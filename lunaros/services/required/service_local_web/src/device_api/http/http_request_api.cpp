#include "lunaros_network.h"
#include "diagnostics/function_probe.h"
#include "device_api/http/http_request_api.h"

#include <string>
#include <cstring>
#include <vector>

#include <ArduinoJson.h>
#include <esp_err.h>
#include <esp_timer.h>
#include <cstdio>
#include <cerrno>
#include <lwip/sockets.h>
#include <esp_log.h>
#include "device_api/http/response_batch.h"
#include "device_api/http/bounded_json_writer.h"
#include "device_api/http/query_connections.h"

#include "device_api/http/http_request_context.h"
#include "device_api/http/http_status.h"
#include "device_api/access/device_api_access_policy.h"
#include "lunaros_platform_link.h"

namespace {

thread_local lunaredu::lunaros::internal::ResponseBatch *response_batch = nullptr;

// This server owns plain HTTP sockets (TLS is not used by this host).
int plain_socket_send(httpd_handle_t, int fd, const char *data, size_t size, int flags) {
  const int sent = ::send(fd, data, size, flags);
  if (sent >= 0) return sent;
  return errno == EAGAIN || errno == EINTR ? HTTPD_SOCK_ERR_TIMEOUT : HTTPD_SOCK_ERR_FAIL;
}
int batch_socket_send(httpd_handle_t server, int fd, const char *data, size_t size, int flags) {
  if (!response_batch) return plain_socket_send(server, fd, data, size, flags);
  return response_batch->append(data, size) ? static_cast<int>(size) : HTTPD_SOCK_ERR_FAIL;
}

esp_err_t send_small_response(httpd_req_t *request, const char *body, size_t body_size) {
  if (body_size > lunaredu::lunaros::internal::ResponseBatch::body_limit)
    return httpd_resp_send(request, body, body_size);
  lunaredu::lunaros::internal::ResponseBatch batch(body, body_size,
    g_request_context?&g_request_context->response_headers:nullptr);
  const int fd = httpd_req_to_sockfd(request);
  if (httpd_sess_set_send_override(request->handle, fd, batch_socket_send) != ESP_OK)
    return ESP_FAIL;
  response_batch = &batch;
  const esp_err_t formatted = httpd_resp_send(request, body, body_size);
  response_batch = nullptr;
  (void)httpd_sess_set_send_override(request->handle, fd, plain_socket_send);
  if (formatted != ESP_OK) return formatted;
  return batch.flush([&](const char *headers, size_t header_size,
                        const char *data, size_t size) {
    iovec vectors[2]{};
    msghdr message{};
    if (header_size) {
      vectors[message.msg_iovlen++] = {const_cast<char *>(headers), header_size};
    }
    if (size) vectors[message.msg_iovlen++] = {const_cast<char *>(data), size};
    message.msg_iov = vectors;
    return static_cast<int>(::sendmsg(fd, &message, 0));
  }) ? ESP_OK : ESP_FAIL;
}

void close_failed_response(httpd_req_t *request) {
  const int fd=httpd_req_to_sockfd(request);
  if (httpd_sess_trigger_close(request->handle,fd)!=ESP_OK) {
    // If the HTTP control queue is full, still deliver EOF to a truncated
    // response. Keep descriptor ownership with httpd; never close it here.
    (void)::shutdown(fd,SHUT_RDWR);
  }
}

void set_response_headers(HttpRequestContext &context, int status_code,
                          const char *content_type) {
  httpd_resp_set_status(
      context.request, lunaredu::lunaros::internal::http_status_text(status_code));
  if (content_type != nullptr) {
    httpd_resp_set_type(context.request, content_type);
  }
  // Reuse a bounded subset of successful API query sessions. Closing every
  // response repeats TCP admission/teardown at the workbench's normal cadence;
  // retaining every browser connection exhausts the small HTTP pool instead.
  // Each request still passes the full origin/grant checks independently.
  const bool query=context.method==HTTP_GET&&status_code==200&&
      std::strncmp(context.request->uri,"/api/v1/",8)==0;
  const bool retained=lunaredu::lunaros::internal::query_connections().retain(
      httpd_req_to_sockfd(context.request),query);
  httpd_resp_set_hdr(context.request, "Connection", retained?"keep-alive":"close");
  // Separate device work from network round-trip latency without exposing
  // request bodies, control tokens or account credentials.
  if (context.started_us > 0) {
    char timing[48];
    std::snprintf(timing, sizeof(timing), "app;dur=%.3f",
                  (esp_timer_get_time() - context.started_us) / 1000.0);
    context.response_timing = timing;
    httpd_resp_set_hdr(context.request, "Server-Timing", context.response_timing.c_str());
  }
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

bool add_cors_headers(HttpRequestContext &context) {
  const std::string origin = header_value(context.request, "Origin");
  if (!device_api_origin_allowed(origin.c_str(),
          lunaredu::lunaros::network_snapshot().local_ip.c_str(),
          lunaredu::lunaros::platform_link_binding_browser_origin().c_str())) {
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
                                       "Content-Type, X-Lunar-Debug-Token, X-Lunar-Access-Grant");
  context.pending_headers.emplace_back("Access-Control-Max-Age", "600");
  if (header_value(context.request,
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

void send_json_response(int status_code, const std::string &body) {
  send_http_response(status_code, "application/json; charset=utf-8", body);
}

void send_http_response(int status_code, const char *content_type,
                        const std::string &body) {
  lunaredu::system::probe::Scope diagnostic_reply(lunaredu::system::probe::Point::HttpReply);
  if (g_request_context == nullptr) {
    return;
  }

  (void)add_cors_headers(*g_request_context);
  set_response_headers(*g_request_context, status_code, content_type);
  const int64_t send_started = esp_timer_get_time();
  const esp_err_t result = send_small_response(g_request_context->request, body.data(), body.size());
  const int64_t send_ms = (esp_timer_get_time() - send_started) / 1000;
  if (result != ESP_OK || send_ms >= 100) {
    ESP_LOGW("device_api_http", "response send status=%d bytes=%u elapsed_ms=%lld err=%s",
         status_code, static_cast<unsigned>(body.size()),
         static_cast<long long>(send_ms), esp_err_to_name(result));
  }
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

void add_response_header(const char *name, const char *value) {
  if (g_request_context == nullptr || name == nullptr || value == nullptr) {
    return;
  }
  g_request_context->pending_headers.emplace_back(name, value);
}

void serialize_and_send_json(int status_code, JsonDocument &doc) {
  lunaredu::system::probe::Scope diagnostic_reply(lunaredu::system::probe::Point::HttpReply);
  if (g_request_context == nullptr || g_request_context->request == nullptr) return;
  auto &context=*g_request_context;
  (void)add_cors_headers(context);
  // ArduinoJson can fail an allocation while constructing the document. Never
  // send the partial tree as a successful capabilities/status response.
  if (doc.overflowed()) {
    set_response_headers(context,503,"application/json; charset=utf-8");
    constexpr char error[]= "{\"ok\":false,\"error\":{\"code\":\"response_document_overflow\"}}";
    if (send_small_response(context.request,error,sizeof(error)-1)!=ESP_OK)
      close_failed_response(context.request);
    return;
  }
  set_response_headers(context,status_code,"application/json; charset=utf-8");
  const size_t measured=measureJson(doc);
  if(measured>1024 && measured<=lunaredu::lunaros::internal::ResponseBatch::capacity && context.json_workspace){
    // Reuse the existing bounded, retiring request allocator. Normal status
    // replies fit here and can use the SDK's complete-body send instead of
    // multiple small TCP chunks. Larger documents keep the fixed stream below.
    auto *buffer=static_cast<char *>(context.json_workspace->allocate(measured+1));
    if(buffer){
      const auto written=serializeJson(doc,buffer,measured+1);
      const auto sent=written==measured?send_small_response(context.request,buffer,written):ESP_FAIL;
      std::memset(buffer,0,measured+1);
      context.json_workspace->deallocate(buffer);
      if(sent!=ESP_OK)close_failed_response(context.request);
      return;
    }
    // A staging allocation is optional. The unchanged bounded stream can
    // still send the complete document without turning it into an OOM reply.
  }
  bool chunked=false;
  lunaredu::lunaros::internal::BoundedJsonWriter writer(
      [&](const char *data,size_t size,bool final) {
        // Retain the existing one-write batching path for small JSON replies.
        if (!chunked && final)
          return send_small_response(context.request,data,size)==ESP_OK;
        chunked=true;
        if (size && httpd_resp_send_chunk(context.request,data,size)!=ESP_OK) return false;
        return !final || httpd_resp_send_chunk(context.request,nullptr,0)==ESP_OK;
      });
  (void)serializeJson(doc,writer);
  if (!writer.finish()) {
    ESP_LOGW("device_api_http","JSON response truncated status=%d",status_code);
    // A failed chunk must not be followed by the zero-length success terminator.
    close_failed_response(context.request);
  }
}

void send_segmented_json(int status_code,HttpJsonStreamProducer producer,void *opaque) {
  lunaredu::system::probe::Scope diagnostic_reply(lunaredu::system::probe::Point::HttpReply);
  if(!g_request_context||!g_request_context->request||!producer)return;
  auto &context=*g_request_context;
  (void)add_cors_headers(context);
  set_response_headers(context,status_code,"application/json; charset=utf-8");
  lunaredu::lunaros::internal::BoundedJsonWriter writer(
      [&](const char *data,size_t size,bool final) {
        if(size&&httpd_resp_send_chunk(context.request,data,size)!=ESP_OK)return false;
        return !final||httpd_resp_send_chunk(context.request,nullptr,0)==ESP_OK;
      });
  using Writer=decltype(writer);
  lunaredu::lunaros::internal::JsonStreamSink sink{&writer,
    [](void *owner,const uint8_t *data,size_t size){return static_cast<Writer *>(owner)->write(data,size);}};
  // Never terminate a partially generated or partially sent JSON body as success.
  if(!producer(sink,opaque)||sink.failed||!writer.finish())
    close_failed_response(context.request);
}

void handle_options() {
  const std::string origin = request_header("Origin");
  if (!device_api_origin_allowed(origin.c_str(),
          lunaredu::lunaros::network_snapshot().local_ip.c_str(),
          lunaredu::lunaros::platform_link_binding_browser_origin().c_str())) {
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

std::string request_header(const char *name) {
  return g_request_context == nullptr ? std::string()
                                      : header_value(g_request_context->request, name);
}

void *current_request_context_handle() { return g_request_context; }

std::string request_context_header(const char *name, void *context) {
  auto *request = static_cast<HttpRequestContext *>(context);
  return request && request->request ? header_value(request->request, name) : std::string{};
}
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
