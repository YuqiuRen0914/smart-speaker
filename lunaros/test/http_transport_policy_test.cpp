#include <cassert>
#include <iostream>

#include "network/http/network_transport_policy.h"

using lunaredu::lunaros::NetworkTransportSecurity;

int main() {
  using lunaredu::lunaros::network_transport_policy_failure;
  using lunaredu::lunaros::network_transport_security_allows;

  assert(network_transport_security_allows(
      "https://api.lunaredu.com/device", NetworkTransportSecurity::RequireTls,
      true));
  assert(!network_transport_security_allows(
      "http://api.lunaredu.com/device", NetworkTransportSecurity::RequireTls,
      true));
  assert(network_transport_security_allows(
      "http://192.168.4.1/setup",
      NetworkTransportSecurity::AllowPlaintextDevelopment, true));
  assert(network_transport_security_allows(
      "http://tools.lunaredu.10.253.35.143.nip.io/firmware.bin",
      NetworkTransportSecurity::AllowPlaintextDevelopment, true));
  assert(!network_transport_security_allows(
      "http://api.lunaredu.com/firmware.bin",
      NetworkTransportSecurity::AllowPlaintextDevelopment, true));
  assert(!network_transport_security_allows(
      "http://tools.lunaredu.10.253.35.143.nip.io:13001/firmware.bin",
      NetworkTransportSecurity::AllowPlaintextDevelopment, true));
  assert(network_transport_security_allows(
      "http://dev.lunaredu.local/resource",
      NetworkTransportSecurity::RequireTls, false));
  assert(!network_transport_security_allows(
      "ftp://example.com/resource", NetworkTransportSecurity::RequireTls,
      false));
  assert(network_transport_policy_failure(
      lunaredu::lunaros::kNetworkTransportPolicyError));
  assert(!network_transport_policy_failure("network_transport_requires_job"));

  std::cout << "http transport policy tests passed: 9\n";
  return 0;
}
