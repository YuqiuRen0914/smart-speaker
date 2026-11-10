#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string>

#include <ArduinoJson.h>
#include <esp_http_server.h>

using HTTPMethod = int;
using HttpSimpleHandler = void (*)();
using HttpBodyBufferPolicy = bool (*)();

bool register_http_route(const char *uri, HTTPMethod method,
                         HttpSimpleHandler handler);
bool register_http_route_with_body_policy(const char *uri, HTTPMethod method,
                                          HttpSimpleHandler handler,
                                          HttpBodyBufferPolicy body_policy);
void register_http_not_found_handler(HttpSimpleHandler handler);

std::string current_sta_ip_string();
std::string current_softap_ip_string();
std::string current_request_peer_address();

void send_json_response(int status_code, const std::string &body);
void send_http_response(int status_code, const char *content_type,
                        const std::string &body);
void send_http_response(int status_code, const char *content_type,
                        const char *body);
bool send_http_binary_response(int status_code, const char *content_type,
                               const uint8_t *data, size_t size);

bool begin_http_stream(int status_code, const char *content_type);
bool send_http_chunk(const uint8_t *data, size_t size);
void end_http_stream();
void add_response_header(const char *name, const char *value);

void serialize_and_send_json(int status_code, JsonDocument &doc);
void handle_options();

bool parse_json_body(JsonDocument &doc);
bool request_has_arg(const char *name);
std::string request_arg(const char *name);
HTTPMethod current_request_method();
std::string current_request_uri();

const uint8_t *request_body_data();
size_t request_body_size();
size_t request_content_length();
int read_request_body_chunk(uint8_t *buffer, size_t max_size);
std::string request_header(const char *name);

// Opaque bridge used while a request is synchronously dispatched to its
// declared LunarOS owner executor. The HTTP worker remains blocked until the
// owner completes, so the request lifetime stays valid without exposing
// HttpRequestContext outside the transport.
void *current_request_context_handle();
bool request_context_has_arg(const char *name, void *context);
std::string request_context_arg(const char *name, void *context);
int read_request_body_chunk_from_context(uint8_t *buffer, size_t max_size,
                                         void *context);
bool send_http_binary_response_from_context(
    int status_code, const char *content_type, const uint8_t *data,
    size_t size, void *context);
bool begin_http_stream_from_context(int status_code, const char *content_type,
                                    void *context);
bool send_http_chunk_from_context(const uint8_t *data, size_t size,
                                  void *context);
void end_http_stream_from_context(void *context);
