#pragma once
#include "lunaros_network_http_client.h"
#include <memory>
namespace lunaredu::lunaros {
class NetworkRequestStream {
public:
  virtual ~NetworkRequestStream() = default;
  virtual int Read(char *buffer, size_t buffer_size) = 0;
  virtual int GetStatusCode() = 0;
  virtual size_t GetBodyLength() = 0;
  virtual int GetLastError() = 0;
  virtual std::string GetLastErrorDetail() = 0;
  // Only explicitly requested response headers are captured, bounded by the
  // transport. Other backends may expose no headers.
  virtual std::string GetHeader(const std::string &) { return {}; }
  virtual void Close() = 0;
  // Concurrent stop request only. Close and destruction still belong to the
  // I/O owner, after its current SDK call returns.
  virtual bool RequestCancel() { return false; }
};

struct NetworkRequest {
  std::string method;
  std::string url;
  std::vector<NetworkHttpHeader> headers;
  std::optional<std::string> body;
  NetworkBodyStream body_stream;
  bool keep_alive = true;
  std::vector<std::string> response_header_keys;
  // Optional explicit authentication scope for a multi-request protocol.
  // Empty uses exact request headers. SDK request headers are always cleared
  // on return; scopes never bypass origin/trust/radio-generation checks.
  std::string connection_scope;
  int timeout_ms = 30000;
  NetworkTransportSecurity transport_security =
      NetworkTransportSecurity::RequireTls;
  bool system_trust_only = false;
  std::stop_token cancellation;
  // Zero retains the streaming consumer's existing per-I/O timeout. An
  // explicit total budget spans setup, headers and all body chunks.
  int total_timeout_ms=0;
};

std::unique_ptr<NetworkRequestStream> open_network_request_stream(NetworkRequest, int *, std::string *);
}
