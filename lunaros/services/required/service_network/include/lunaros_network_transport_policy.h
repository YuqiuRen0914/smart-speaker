#pragma once

#include "lunaros_network_http_client.h"

namespace lunaredu::lunaros {

inline constexpr size_t kNetworkMinimumTlsBlockBytes = 12288;
constexpr bool network_tls_memory_available(bool tls_required, size_t internal_largest) {
  return !tls_required || internal_largest >= kNetworkMinimumTlsBlockBytes;
}

inline constexpr const char *kNetworkTransportPolicyError =
    "transport_policy_tls_required";

bool network_transport_policy_failure(const std::string &detail);

} // namespace lunaredu::lunaros
