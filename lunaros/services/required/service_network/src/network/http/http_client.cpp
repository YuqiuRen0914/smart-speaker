#include "lunaros_network_lifecycle.h"
#include "network/network_service_internal.h"
#include "lunaros_network_http_client.h"

#include <esp_err.h>
#include <esp_log.h>

#include <array>
#include <utility>

#include "lunaros_network_jobs.h"
#include "network/http/http_request_internal.h"

namespace lunaredu::lunaros {

namespace {

constexpr const char *kTag = "network_http";

bool fill_response_from_stream(NetworkRequestStream *stream,
                               NetworkResponse *response,
                               size_t maximum_response_bytes,
                               bool head_request) {
  if (stream == nullptr || response == nullptr) {
    return false;
  }

  response->body.clear();
  response->last_error = 0;
  response->error_detail.clear();
  response->status_code = stream->GetStatusCode();
  if (response->status_code < 0) {
    response->last_error = stream->GetLastError();
    response->error_detail = stream->GetLastErrorDetail();
    stream->Close();
    return false;
  }
  // HEAD and bodyless statuses may advertise representation length without
  // carrying it. Do not read or reject them as truncated response bodies.
  if (head_request || response->status_code == 204 || response->status_code == 304) {
    stream->Close();
    return true;
  }
  const size_t declared_length = stream->GetBodyLength();
  if (maximum_response_bytes == 0 ||
      (declared_length > 0 && declared_length > maximum_response_bytes)) {
    response->body.clear();
    response->last_error = ESP_ERR_INVALID_SIZE;
    response->error_detail = "response_body_too_large";
    stream->Close();
    return false;
  }

  response->body.clear();
  if (declared_length > 0) {
    response->body.reserve(declared_length);
  }
  std::array<char, 1024> buffer{};
  for (;;) {
    const int read = stream->Read(buffer.data(), buffer.size());
    if (read < 0) {
      response->body.clear();
      response->last_error = stream->GetLastError();
      response->error_detail = stream->GetLastErrorDetail();
      stream->Close();
      return false;
    }
    if (read == 0) {
      break;
    }
    if (static_cast<size_t>(read) > buffer.size() ||
        static_cast<size_t>(read) > maximum_response_bytes - response->body.size()) {
      response->body.clear();
      response->last_error = ESP_ERR_INVALID_SIZE;
      response->error_detail = "response_body_too_large";
      stream->Close();
      return false;
    }
    response->body.append(buffer.data(), static_cast<size_t>(read));
  }
  response->last_error = stream->GetLastError();
  response->error_detail = stream->GetLastErrorDetail();
  const bool complete = response->last_error == 0 &&
      (declared_length == 0 || response->body.size() == declared_length);
  if (!complete) {
    response->body.clear();
    if (response->last_error == 0) {
      response->last_error = ESP_ERR_INVALID_SIZE;
      response->error_detail = "response_body_incomplete";
    }
  }
  stream->Close();
  return complete;
}

std::unique_ptr<NetworkRequestStream>
execute_request(NetworkRequest request, int *failure_error = nullptr,
                std::string *failure_error_detail = nullptr, bool *failure_deferred = nullptr) {
  if (failure_deferred != nullptr) *failure_deferred = false;
  if(request.timeout_ms<=0||request.total_timeout_ms<0) {
    if(failure_error)*failure_error=ESP_ERR_INVALID_ARG;
    if(failure_error_detail)*failure_error_detail="invalid_network_request_deadline";
    return nullptr;
  }
  if (!network_job_runner_context_active()) {
    if (failure_error != nullptr) {
      *failure_error = ESP_ERR_INVALID_STATE;
    }
    if (failure_error_detail != nullptr) {
      *failure_error_detail = "network_transport_requires_job";
    }
    return nullptr;
  }
  if ((request.system_trust_only && request.transport_security!=NetworkTransportSecurity::RequireTls) ||
      !network_transport_security_allows(
          request.url, request.transport_security)) {
    if (failure_error != nullptr) {
      *failure_error = ESP_ERR_INVALID_ARG;
    }
    if (failure_error_detail != nullptr) {
      *failure_error_detail = kNetworkTransportPolicyError;
    }
    return nullptr;
  }
  return create_esp_http_request_stream(std::move(request), failure_error,
                                        failure_error_detail, failure_deferred);
}

} // namespace

std::unique_ptr<NetworkRequestStream> open_network_request_stream(NetworkRequest request, int *error, std::string *detail) {
  return execute_request(std::move(request), error, detail);
}

bool network_fetch_resource(NetworkFetchRequest request,
                            NetworkResponse *response) {
  if (response == nullptr) {
    return false;
  }
  response->deferred = false;
  const bool head_request = request.method == "HEAD";
  NetworkRequest http_request{};
  http_request.method = std::move(request.method);
  http_request.url = std::move(request.url);
  http_request.headers = std::move(request.headers);
  http_request.body = std::move(request.body);
  http_request.body_stream = request.body_stream;
  http_request.timeout_ms = request.timeout_ms;
  http_request.cancellation = request.cancellation;
  http_request.total_timeout_ms=request.total_timeout_ms>0?request.total_timeout_ms:request.timeout_ms;
  http_request.transport_security = request.transport_security;
  http_request.system_trust_only = request.system_trust_only;

  int failure_error = 0;
  std::string failure_error_detail;
  auto stream = execute_request(std::move(http_request), &failure_error,
                                &failure_error_detail, &response->deferred);
  if (stream == nullptr) {
    response->status_code = -1;
    response->body.clear();
    response->last_error = failure_error;
    response->error_detail = std::move(failure_error_detail);
    return false;
  }
  const bool completed = fill_response_from_stream(
      stream.get(), response, request.maximum_response_bytes, head_request);
  return completed;
}

} // namespace lunaredu::lunaros
