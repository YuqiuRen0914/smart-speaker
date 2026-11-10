#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <stop_token>
#include <vector>

namespace lunaredu::lunaros {

enum class DownloadTransportSecurity {
  RequireTls,
};

struct DownloadHeader {
  std::string key;
  std::string value;
};

struct DownloadProgress {
  size_t total_read = 0;
  size_t content_length = 0;
  int progress = 0;
  size_t speed = 0;
};

using DownloadProgressCallback = std::function<void(const DownloadProgress &)>;

struct DownloadRequest {
  std::string url;
  std::vector<DownloadHeader> headers;
  int timeout_ms = 30000;
  DownloadTransportSecurity transport_security =
      DownloadTransportSecurity::RequireTls;
  DownloadProgressCallback progress_callback;
  bool keep_alive = false;
  size_t maximum_bytes = 0;
  std::stop_token cancellation;
  int total_timeout_ms=0;
};

struct DownloadFailure {
  int status_code = -1;
  int error = 0;
  std::string detail;
};

class DownloadReader {
 public:
  virtual ~DownloadReader() = default;
  virtual int Read(char *buffer, size_t buffer_size) = 0;
  virtual size_t content_length() const = 0;
  virtual size_t total_read() const = 0;
  virtual int GetLastError() const = 0;
  virtual std::string error_detail() const { return {}; }
  virtual void Close() = 0;
};

struct DownloadTransportResult {
  std::unique_ptr<DownloadReader> reader;
  int status_code = -1;
  size_t content_length = 0;
  int error = 0;
  std::string detail;
};

// OS opens a transport stream. Download validates and bounds the transfer.
using DownloadTransport = DownloadTransportResult (*)(DownloadRequest);

bool download_service_start(DownloadTransport transport);
void download_service_stop();
bool download_service_ready();
std::unique_ptr<DownloadReader> download_start(
    DownloadRequest request, DownloadFailure *failure = nullptr);

}  // namespace lunaredu::lunaros

