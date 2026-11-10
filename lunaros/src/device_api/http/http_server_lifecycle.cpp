#include "device_api/http/embedded_http_server.h"

#include <cerrno>
#include <string>

#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <lwip/sockets.h>

#include "lunaredu_lunaros/device_api/device_api.h"
#include "lunaredu_lunaros/kernel/log.h"
#include "device_api/http/embedded_http_server_internal.h"
#include "sdkconfig.h"

#ifndef CONFIG_LUNAREDU_DEVICE_API_HTTP_SERVER_PORT
#define CONFIG_LUNAREDU_DEVICE_API_HTTP_SERVER_PORT 80
#endif
#ifndef CONFIG_LUNAREDU_DEVICE_API_HTTP_STACK_SIZE
#define CONFIG_LUNAREDU_DEVICE_API_HTTP_STACK_SIZE 12288
#endif
#ifndef CONFIG_LUNAREDU_DEVICE_API_HTTP_SERVER_CORE_ID
#define CONFIG_LUNAREDU_DEVICE_API_HTTP_SERVER_CORE_ID 1
#endif
#ifndef CONFIG_LUNAREDU_DEVICE_API_HTTP_MAX_URI_HANDLERS
#define CONFIG_LUNAREDU_DEVICE_API_HTTP_MAX_URI_HANDLERS 32
#endif

namespace {

void close_http_socket_directly(httpd_handle_t, int sockfd) {
  if (lwip_close(sockfd) != 0) {
    lunaredu::lunaros::report_device_host_connection(
        lunaredu::lunaros::DeviceHostConnectionState::Error);
    LOGW("device_api_http", "lwip_close failed fd=%d errno=%d", sockfd,
         errno);
  }
}

esp_err_t open_http_socket(httpd_handle_t, int) {
  lunaredu::lunaros::report_device_host_connection(
      lunaredu::lunaros::DeviceHostConnectionState::Connected);
  return ESP_OK;
}

void log_http_heap(const char *phase, const httpd_config_t &config) {
  LOGI("device_api_http",
       "%s stack=%u task_caps=0x%x heap_free=%u heap_min=%u sram_free=%u "
       "sram_largest=%u psram_free=%u psram_largest=%u",
       phase, static_cast<unsigned>(config.stack_size),
       static_cast<unsigned>(config.task_caps),
       static_cast<unsigned>(esp_get_free_heap_size()),
       static_cast<unsigned>(esp_get_minimum_free_heap_size()),
       static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
       static_cast<unsigned>(
           heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
       static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
       static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)));
}

} // namespace

bool start_embedded_http_server_if_needed() {
  httpd_handle_t &server = device_api_http_server();
  if (server != nullptr) {
    return false;
  }

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = CONFIG_LUNAREDU_DEVICE_API_HTTP_SERVER_PORT;
  config.stack_size = CONFIG_LUNAREDU_DEVICE_API_HTTP_STACK_SIZE;
  config.core_id = CONFIG_LUNAREDU_DEVICE_API_HTTP_SERVER_CORE_ID;
  config.task_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
  config.uri_match_fn = httpd_uri_match_wildcard;
  config.lru_purge_enable = true;
  config.open_fn = open_http_socket;
  config.close_fn = close_http_socket_directly;
  config.max_uri_handlers = CONFIG_LUNAREDU_DEVICE_API_HTTP_MAX_URI_HANDLERS;
  LOGI("device_api_http", "starting http server port=%u max_handlers=%u",
       static_cast<unsigned>(config.server_port),
       static_cast<unsigned>(config.max_uri_handlers));
  log_http_heap("http server start begin", config);
  if (httpd_start(&server, &config) != ESP_OK) {
    server = nullptr;
    log_http_heap("http server start failed", config);
    return false;
  }
  log_http_heap("http server start ok", config);

  if (!register_device_api_routes_with_server()) {
    LOGE("device_api_http", "HTTP route registration incomplete");
    httpd_stop(server);
    server = nullptr;
    return false;
  }
  install_device_api_not_found_handler();
  ESP_LOGD("device_api_http", "HTTP ready routes=%u max_handlers=%u",
           static_cast<unsigned>(device_api_route_count()),
           static_cast<unsigned>(config.max_uri_handlers));
  return true;
}

bool stop_embedded_http_server(std::string &error) {
  error.clear();
  httpd_handle_t &server = device_api_http_server();
  if (server == nullptr) {
    return true;
  }
  const esp_err_t result = httpd_stop(server);
  if (result != ESP_OK) {
    error = std::string("device_api_http_stop_failed:") +
            esp_err_to_name(result);
    return false;
  }
  server = nullptr;
  return true;
}

httpd_handle_t embedded_http_server_handle() {
  return device_api_http_server();
}
