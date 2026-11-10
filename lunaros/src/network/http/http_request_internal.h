#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "network/http/network_transport_policy.h"

namespace lunaredu::lunaros {

class NetworkRequestStream {
public:
  virtual ~NetworkRequestStream() = default;
  virtual int Read(char *buffer, size_t buffer_size) = 0;
  virtual int GetStatusCode() = 0;
  virtual size_t GetBodyLength() = 0;
  virtual int GetLastError() = 0;
  virtual std::string GetLastErrorDetail() = 0;
  virtual void Close() = 0;
};

struct NetworkRequest {
  std::string method;
  std::string url;
  std::vector<NetworkHttpHeader> headers;
  std::optional<std::string> body;
  bool keep_alive = false;
  int timeout_ms = 30000;
  NetworkTransportSecurity transport_security =
      NetworkTransportSecurity::RequireTls;
};

std::unique_ptr<NetworkRequestStream>
create_esp_http_request_stream(NetworkRequest request, int *failure_error,
                               std::string *failure_error_detail);

std::unique_ptr<NetworkDownloadReader> create_network_download_reader(
    std::unique_ptr<NetworkRequestStream> stream, size_t content_length,
    NetworkDownloadProgressCallback callback);

} // namespace lunaredu::lunaros
