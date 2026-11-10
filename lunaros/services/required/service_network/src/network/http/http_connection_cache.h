#pragma once
#include "network/http/tls_session_cache.h"
#include "lunaros_network_request_stream.h"
#include <esp_http_client.h>
namespace lunaredu::lunaros {
struct HttpConnectionLease {
  esp_http_client_handle_t client = nullptr;
  HttpTlsSessionLease tls;
  uint64_t generation = 0;
  explicit operator bool() const { return generation != 0; }
};
// One bounded warm connection. Busy/untrusted/low-memory requests bypass it.
// The I/O owner alone transfers SDK handles. Invalidation never destroys a
// borrowed client; expiry cleanup is submitted to the existing I/O executor.
HttpConnectionLease acquire_http_connection(const NetworkRequest &,
    std::shared_ptr<const NetworkTlsPolicy>);
bool retain_http_connection(HttpConnectionLease &, esp_http_client_handle_t &,
    HttpTlsSessionLease &);
void release_http_connection(HttpConnectionLease &);
void poll_http_connections();
bool http_connection_owned();
}
