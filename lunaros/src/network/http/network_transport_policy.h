#pragma once

#include "lunaredu_lunaros/network/http_client.h"

namespace lunaredu::lunaros {

inline constexpr size_t kNetworkMinimumTlsBlockBytes = 12288;
constexpr bool network_tls_memory_available(bool tls_required, size_t internal_largest) {
  return !tls_required || internal_largest >= kNetworkMinimumTlsBlockBytes;
}

inline constexpr const char *kNetworkTransportPolicyError =
    "transport_policy_tls_required";

bool network_transport_security_allows(
    const std::string &url, NetworkTransportSecurity requested_security,
    bool require_tls_for_production);
bool network_transport_policy_failure(const std::string &detail);

} // namespace lunaredu::lunaros
