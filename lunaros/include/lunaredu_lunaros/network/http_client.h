#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace lunaredu::lunaros {

enum class NetworkTransportSecurity {
  RequireTls,
  AllowPlaintextDevelopment,
};

struct NetworkHttpHeader {
  std::string key;
  std::string value;
};

struct NetworkResponse {
  int status_code = -1;
  std::string body;
  int last_error = 0;
  std::string error_detail;
};

struct NetworkDownloadProgress {
  size_t total_read = 0;
  size_t content_length = 0;
  int progress = 0;
  size_t speed = 0;
};

using NetworkDownloadProgressCallback =
    std::function<void(const NetworkDownloadProgress &)>;

struct NetworkDownloadRequest {
  std::string url;
  std::vector<NetworkHttpHeader> headers;
  int timeout_ms = 30000;
  NetworkTransportSecurity transport_security =
      NetworkTransportSecurity::RequireTls;
  NetworkDownloadProgressCallback progress_callback;
  bool keep_alive = false;
};

struct NetworkFetchRequest {
  std::string method = "GET";
  std::string url;
  std::vector<NetworkHttpHeader> headers;
  std::optional<std::string> body;
  int timeout_ms = 30000;
  // Ordinary control/configuration responses are buffered in memory. Keep a
  // hard default so a malformed server or captive portal cannot exhaust the
  // device heap through an unbounded buffered read. Streaming downloads use the
  // separate NetworkDownloadRequest path.
  size_t maximum_response_bytes = 256U * 1024U;
  NetworkTransportSecurity transport_security =
      NetworkTransportSecurity::RequireTls;
};

struct NetworkDownloadFailure {
  int status_code = -1;
  int error = 0;
  std::string detail;
};

class NetworkDownloadReader {
 public:
  virtual ~NetworkDownloadReader() = default;

  virtual int Read(char *buffer, size_t buffer_size) = 0;
  virtual size_t content_length() const = 0;
  virtual size_t total_read() const = 0;
  virtual int GetLastError() const = 0;
  virtual std::string error_detail() const { return {}; }
  virtual void Close() = 0;
};

std::unique_ptr<NetworkDownloadReader>
network_start_download(NetworkDownloadRequest request,
                       NetworkDownloadFailure *failure = nullptr);

bool network_fetch_resource(NetworkFetchRequest request,
                            NetworkResponse *response);

}  // namespace lunaredu::lunaros
