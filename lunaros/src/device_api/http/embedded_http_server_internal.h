#pragma once

#include <cstddef>

#include <esp_http_server.h>

httpd_handle_t &device_api_http_server();
bool register_device_api_routes_with_server();
void install_device_api_not_found_handler();
size_t device_api_route_count();
