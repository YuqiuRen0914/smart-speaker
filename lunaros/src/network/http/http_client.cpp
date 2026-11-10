#include "network/network_service_internal.h"
#include "lunaredu_lunaros/network/http_client.h"

#include <esp_err.h>
#include <esp_log.h>

#include <array>
#include <utility>

#include "lunaredu_lunaros/network/network.h"
#include "lunaredu_lunaros/network/network_jobs.h"
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
                std::string *failure_error_detail = nullptr) {
  if (!network_job_runner_context_active()) {
    if (failure_error != nullptr) {
      *failure_error = ESP_ERR_INVALID_STATE;
    }
    if (failure_error_detail != nullptr) {
      *failure_error_detail = "network_transport_requires_job";
    }
    return nullptr;
  }
  if (!network_transport_security_allows(
          request.url, request.transport_security,
          network_service_internal().policy().require_tls_for_production)) {
    if (failure_error != nullptr) {
      *failure_error = ESP_ERR_INVALID_ARG;
    }
    if (failure_error_detail != nullptr) {
      *failure_error_detail = kNetworkTransportPolicyError;
    }
    return nullptr;
  }
  return create_esp_http_request_stream(std::move(request), failure_error,
                                        failure_error_detail);
}

} // namespace

bool network_fetch_resource(NetworkFetchRequest request,
                            NetworkResponse *response) {
  if (response == nullptr) {
    return false;
  }
  const bool head_request = request.method == "HEAD";
  NetworkRequest http_request{};
  http_request.method = std::move(request.method);
  http_request.url = std::move(request.url);
  http_request.headers = std::move(request.headers);
  http_request.body = std::move(request.body);
  http_request.timeout_ms = request.timeout_ms;
  http_request.transport_security = request.transport_security;

  int failure_error = 0;
  std::string failure_error_detail;
  auto stream = execute_request(std::move(http_request), &failure_error,
                                &failure_error_detail);
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

std::unique_ptr<NetworkDownloadReader>
network_start_download(NetworkDownloadRequest request, NetworkDownloadFailure *failure) {
  if (failure) *failure = {};
  NetworkRequest http_request{};
  http_request.method = "GET";
  http_request.keep_alive = request.keep_alive;
  http_request.url = std::move(request.url);
  http_request.headers = std::move(request.headers);
  http_request.timeout_ms = request.timeout_ms;
  http_request.transport_security = request.transport_security;

  int failure_error = 0;
  std::string failure_error_detail;
  auto stream = execute_request(std::move(http_request), &failure_error,
                                &failure_error_detail);
  if (stream == nullptr) {
    if (failure) { failure->error = failure_error; failure->detail = failure_error_detail; }
    ESP_LOGE(kTag, "failed to open download stream: %s",
             failure_error_detail.empty() ? "transport_error"
                                          : failure_error_detail.c_str());
    return nullptr;
  }

  const int status_code = stream->GetStatusCode();
  if (failure) failure->status_code = status_code;
  if (status_code != 200) {
    if (failure) { failure->error = stream->GetLastError(); failure->detail = stream->GetLastErrorDetail(); }
    ESP_LOGE(kTag, "unexpected download status code: %d", status_code);
    stream->Close();
    return nullptr;
  }

  const size_t content_length = stream->GetBodyLength();
  if (content_length == 0) {
    if (failure) { failure->error = ESP_ERR_INVALID_SIZE; failure->detail = "download_content_length_unavailable"; }
    ESP_LOGE(kTag, "download content length is unavailable");
    stream->Close();
    return nullptr;
  }

  return create_network_download_reader(
      std::move(stream), content_length, std::move(request.progress_callback));
}

} // namespace lunaredu::lunaros
