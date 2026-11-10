#pragma once

#include <utility>
#include <vector>
#include "memory/json_workspace.h"
#include "device_api/http/response_batch.h"

#include "device_api/http/http_request_api.h"

struct HttpRequestContext {
  httpd_req_t *request = nullptr;
  int64_t started_us = 0;
  std::string response_timing;
  HTTPMethod method = HTTP_GET;
  std::vector<std::pair<std::string, std::string>> args;
  std::vector<uint8_t> body;
  std::vector<std::pair<std::string, std::string>> pending_headers;
  std::vector<char> query;
  std::string serialized;
  lunaredu::lunaros::JsonWorkspace *json_workspace=nullptr;
  lunaredu::lunaros::internal::ResponseBatch::HeaderStorage response_headers;
};

extern thread_local HttpRequestContext *g_request_context;

std::string header_value(httpd_req_t *request, const char *name);
