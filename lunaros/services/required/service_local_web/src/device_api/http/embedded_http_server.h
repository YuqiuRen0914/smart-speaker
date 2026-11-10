#pragma once

#include <string>
namespace lunaredu::lunaros { class DeviceProjectionRegistry; }

#include <esp_http_server.h>

/**
 * @brief 注册 Device API 和配网页相关 HTTP 路由。
 */
void register_device_http_routes(lunaredu::lunaros::DeviceProjectionRegistry &registry);

// 启动 Device API HTTP server；调用方应先完成 route 注册。
// 返回 true 表示本次完成启动。
bool start_embedded_http_server_if_needed();
bool stop_embedded_http_server(std::string &error);

// Drop the route callbacks after the HTTP server has stopped. Route entries
// capture a runtime-owned projection registry, so keeping them past the
// transport lifetime would retain stale runtime references.
void reset_device_http_routes();

// Returns the active esp_http_server handle, or nullptr before the server starts.
httpd_handle_t embedded_http_server_handle();
