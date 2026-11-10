#include "lunaros_network_transport_policy.h"

namespace lunaredu::lunaros {

bool network_transport_security_allows(
    const std::string &url, NetworkTransportSecurity requested_security) {
  // Environment/host never downgrade a TLS request. A protocol may explicitly
  // select an HTTP endpoint; HTTPS is still rejected under that policy so no
  // failed TLS request can silently fall back to plaintext.
  return (requested_security == NetworkTransportSecurity::RequireTls && url.rfind("https://",0)==0) ||
         (requested_security == NetworkTransportSecurity::ExplicitHttp && url.rfind("http://",0)==0);
}

bool network_transport_policy_failure(const std::string &detail) {
  return detail == kNetworkTransportPolicyError;
}

} // namespace lunaredu::lunaros
