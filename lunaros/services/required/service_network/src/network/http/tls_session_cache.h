#pragma once
#include "lunaros_network_tls.h"
#include <cstdint>
struct esp_transport_item_t;
namespace lunaredu::lunaros {
struct HttpTlsSessionLease {
  esp_transport_item_t *transport = nullptr;
  bool offered = false;
  uint64_t generation = 0;
};
// One exclusive, nonblocking lease. Contention/OOM/unsupported URLs use the
// ordinary HTTP transport. Only a session survives; never a live socket.
// Save/release belong to that stream's I/O owner; copies do not transfer
// ownership. Generation rejects stale copies after the handle is reissued.
HttpTlsSessionLease acquire_http_tls_session(const std::string &url,
    std::shared_ptr<const NetworkTlsPolicy> policy, bool keep_alive);
bool save_http_tls_session(HttpTlsSessionLease lease);
void release_http_tls_session(HttpTlsSessionLease &lease, bool saved);
// In-use transports remain owned by their stream and are discarded on return.
void invalidate_http_tls_session();
void poll_http_tls_session();
// Radio/trust invalidation also revokes any borrowed/warm HTTP connection.
uint64_t http_tls_policy_generation();
} // namespace lunaredu::lunaros
