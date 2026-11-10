#include <algorithm>
#include <cassert>
#include <cstring>
#include <iostream>
#include "network/network_service_internal.h"
#include "network/http/http_request_internal.h"

namespace {
std::string body;
size_t declared = 0;
int status = 200;
int transport_error = 0;
bool negative_read = false;
bool oversized_read = false;
size_t reads = 0;
bool closed = false;
bool job_active = true;
bool security_allowed = true;
bool open_failure = false;
int opens = 0;
bool kept_alive = false;
}

namespace lunaredu::lunaros {
class TestStream final : public NetworkRequestStream {
  size_t offset_ = 0;
public:
  int Read(char *buffer, size_t capacity) override {
    ++reads;
    if (negative_read) return -1;
    if (oversized_read) return static_cast<int>(capacity + 1);
    const auto count = std::min(capacity, body.size() - offset_);
    std::memcpy(buffer, body.data() + offset_, count);
    offset_ += count;
    return static_cast<int>(count);
  }
  int GetStatusCode() override { return status; }
  size_t GetBodyLength() override { return declared; }
  int GetLastError() override { return transport_error; }
  std::string GetLastErrorDetail() override { return transport_error ? "transport_error" : ""; }
  void Close() override { closed = true; }
};
NetworkService &network_service_internal() { static NetworkService service; return service; }
NetworkPolicy NetworkService::policy() const { return {}; }
bool network_job_runner_context_active() { return job_active; }
bool network_transport_security_allows(const std::string &, NetworkTransportSecurity, bool) { return security_allowed; }
std::unique_ptr<NetworkRequestStream> create_esp_http_request_stream(NetworkRequest request, int *error, std::string *detail) {
  ++opens;
  kept_alive = request.keep_alive;
  if (open_failure) { *error = 42; *detail = "open_failed"; return {}; }
  return std::make_unique<TestStream>();
}
std::unique_ptr<NetworkDownloadReader> create_network_download_reader(
    std::unique_ptr<NetworkRequestStream>, size_t, NetworkDownloadProgressCallback) { return {}; }
}

int main() {
  using namespace lunaredu::lunaros;
  NetworkFetchRequest request;
  request.url = "https://test.invalid";
  NetworkResponse response;
  body = "{}"; declared = 100;
  assert(!network_fetch_resource(request, &response));
  assert(closed && response.body.empty() && response.last_error != 0);
  assert(response.error_detail == "response_body_incomplete");
  declared = 2;
  assert(network_fetch_resource(request, &response) && response.body == body);
  declared = 1;
  assert(!network_fetch_resource(request, &response) && response.body.empty());
  declared = 0; body.assign(2500, 'x');
  assert(network_fetch_resource(request, &response) && response.body == body);
  request.maximum_response_bytes = 2000;
  assert(!network_fetch_resource(request, &response) && response.body.empty());
  request.maximum_response_bytes = 1; declared = 200000; reads = 0;
  assert(!network_fetch_resource(request, &response) && reads == 0);
  request.method = "HEAD";
  assert(network_fetch_resource(request, &response) && response.body.empty() && reads == 0);
  request.method = "GET";
  for (int code : {204, 304}) {
    status = code;
    assert(network_fetch_resource(request, &response) && response.body.empty() && reads == 0);
  }
  status = 200; declared = 0; body = "{}"; request.maximum_response_bytes = 10;
  transport_error = 42;
  assert(!network_fetch_resource(request, &response) && response.body.empty());
  assert(response.last_error == 42);
  negative_read = true;
  assert(!network_fetch_resource(request, &response) && response.body.empty());
  negative_read = false; transport_error = 0; oversized_read = true;
  assert(!network_fetch_resource(request, &response) && response.body.empty());
  oversized_read = false; status = -1; response.body = "stale";
  assert(!network_fetch_resource(request, &response) && response.body.empty());
  NetworkDownloadRequest download;
  download.url = request.url;
  download.keep_alive = true;
  NetworkDownloadFailure failure;
  job_active = false;
  const int admitted_opens = opens;
  assert(!network_start_download(download, &failure));
  assert(opens == admitted_opens && failure.detail == "network_transport_requires_job");
  job_active = true; security_allowed = false;
  assert(!network_start_download(download, &failure));
  assert(opens == admitted_opens && failure.error != 0);
  security_allowed = true; open_failure = true;
  assert(!network_start_download(download, &failure));
  assert(failure.status_code == -1 && failure.error == 42 && failure.detail == "open_failed");
  assert(kept_alive);
  open_failure = false; status = 404; closed = false;
  assert(!network_start_download(download, &failure));
  assert(closed && failure.status_code == 404);
  status = 200; declared = 0; closed = false;
  assert(!network_start_download(download, &failure));
  assert(closed && failure.status_code == 200 && failure.detail == "download_content_length_unavailable");
  std::cout << "Shared download admission, keep-alive and failure diagnostics passed\n";
  std::cout << "HTTP length, EOF/error, byte bound and HEAD/204/304 checks passed\n";
}
