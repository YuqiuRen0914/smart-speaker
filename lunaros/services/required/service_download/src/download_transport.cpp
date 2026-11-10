#include "lunaros_download_lifecycle.h"
#include "lunaros_network_request_stream.h"
#include "lunaredu_download.h"
#include <esp_log.h>
namespace lunaredu::lunaros {
std::unique_ptr<DownloadReader> create_network_download_reader(std::unique_ptr<NetworkRequestStream>, size_t);
DownloadTransportResult network_open_download_stream(DownloadRequest request) {
  DownloadTransportResult result;
  NetworkRequest http_request{};
  http_request.method = "GET";
  http_request.keep_alive = request.keep_alive;
  http_request.url = std::move(request.url);
  http_request.headers.reserve(request.headers.size());
  for (auto &header : request.headers)
    http_request.headers.push_back({std::move(header.key), std::move(header.value)});
  http_request.timeout_ms = request.timeout_ms;
  http_request.cancellation = request.cancellation;
  http_request.total_timeout_ms=request.total_timeout_ms;
  http_request.transport_security = NetworkTransportSecurity::RequireTls;

  int failure_error = 0;
  std::string failure_error_detail;
  auto stream = open_network_request_stream(std::move(http_request), &failure_error,
                                &failure_error_detail);
  if (stream == nullptr) {
    result.error = failure_error;
    result.detail = std::move(failure_error_detail);
    ESP_LOGE("network_download", "failed to open download stream: %s",
             result.detail.empty() ? "transport_error" : result.detail.c_str());
    return result;
  }

  result.status_code = stream->GetStatusCode();
  result.content_length = stream->GetBodyLength();
  result.error = stream->GetLastError();
  result.detail = stream->GetLastErrorDetail();
  result.reader = create_network_download_reader(
      std::move(stream), result.content_length);
  return result;
}

}
