#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string>
#include <functional>

#include <ArduinoJson.h>
#include <esp_http_server.h>

using HTTPMethod = int;
using HttpSimpleHandler = std::function<void()>;
using HttpBodyBufferPolicy = std::function<bool()>;

bool register_http_route(const char *uri, HTTPMethod method,
                         HttpSimpleHandler handler);
bool register_http_route_with_body_policy(const char *uri, HTTPMethod method,
                                          HttpSimpleHandler handler,
                                          HttpBodyBufferPolicy body_policy);
void register_http_not_found_handler(HttpSimpleHandler handler);


void send_json_response(int status_code, const std::string &body);
void send_http_response(int status_code, const char *content_type,
                        const std::string &body);
void send_http_response(int status_code, const char *content_type,
                        const char *body);
bool send_http_binary_response(int status_code, const char *content_type,
                               const uint8_t *data, size_t size);

void add_response_header(const char *name, const char *value);
void serialize_and_send_json(int status_code, JsonDocument &doc);
void handle_options();

bool parse_json_body(JsonDocument &doc);
bool request_has_arg(const char *name);
std::string request_arg(const char *name);
HTTPMethod current_request_method();
std::string current_request_uri();

