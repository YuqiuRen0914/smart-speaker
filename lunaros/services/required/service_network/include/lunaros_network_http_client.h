#pragma once

#include <functional>
#include <cstdint>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

namespace lunaredu::lunaros {

enum class NetworkTransportSecurity {
  RequireTls,
  // An explicitly configured plaintext protocol endpoint, never a fallback
  // from HTTPS/TLS failure. Existing service requests retain RequireTls.
  ExplicitHttp,
};

// Use the same transport policy for saved service endpoints and HTTP requests.
bool network_transport_security_allows(
    const std::string &url, NetworkTransportSecurity requested_security);

struct NetworkHttpHeader {
  std::string key;
  std::string value;
};
// Borrowed synchronous request body. The reader runs on the admitted I/O owner,
// must honor cancellation, and remains valid until network_fetch_resource returns.
struct NetworkBodyStream {
  size_t length = 0;
  int (*read)(uint8_t *buffer, size_t capacity, void *context) = nullptr;
  void *context = nullptr;
};

struct NetworkResponse {
  int status_code = -1;
  std::string body;
  int last_error = 0;
  std::string error_detail;
  // No request was sent: declared automatic setup yielded for local resource
  // coordination. This is neither a server response nor an unreachable peer.
  bool deferred = false;
};

struct NetworkFetchRequest {
  std::string method = "GET";
  std::string url;
  std::vector<NetworkHttpHeader> headers;
  std::optional<std::string> body;
  NetworkBodyStream body_stream;
  int timeout_ms = 30000;
  // Ordinary control/configuration responses are buffered in memory. Keep a
  // hard default so a malformed server or captive portal cannot exhaust the
  // device heap through an unbounded buffered read. Streaming downloads use the
  // separate DownloadRequest path.
  // Small JSON/control responses by default. Large content uses NetworkStream.
  size_t maximum_response_bytes = 16U * 1024U;
  NetworkTransportSecurity transport_security =
      NetworkTransportSecurity::RequireTls;
  // Fixed maintenance authorities must not inherit business-endpoint custom
  // CAs or skip-verification policies, even if a user selected that origin.
  bool system_trust_only = false;
  // Owned standard cancellation state; requesting stop never closes a socket
  // from the caller's thread. The admitted I/O owner observes and retires it.
  std::stop_token cancellation;
  // Zero uses timeout_ms as the buffered request's complete deadline.
  int total_timeout_ms=0;
};

bool network_fetch_resource(NetworkFetchRequest request,
                            NetworkResponse *response);

}  // namespace lunaredu::lunaros
