#pragma once

#include <utility>
#include <vector>

#include "device_api/http/http_request_api.h"

struct HttpRequestContext {
  httpd_req_t *request = nullptr;
  HTTPMethod method = HTTP_GET;
  std::vector<std::pair<std::string, std::string>> args;
  std::vector<uint8_t> body;
  std::vector<std::pair<std::string, std::string>> pending_headers;
};

extern thread_local HttpRequestContext *g_request_context;

std::string header_value(httpd_req_t *request, const char *name);
