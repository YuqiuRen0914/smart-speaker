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
  for (const auto *url : {"http://speaker.local:8765/xiaozhi/ota/", "http://192.168.31.150:8765/xiaozhi/ota/",
                         "http://10.1.2.3:65535/", "http://172.16.1.2:1/"}) {
    assert(network_transport_security_allows(url, NetworkTransportSecurity::AllowPlaintextLocal, true));
    assert(!network_transport_security_allows(url, NetworkTransportSecurity::RequireTls, true));
    assert(!network_transport_security_allows(url, NetworkTransportSecurity::AllowPlaintextDevelopment, true));
  }
  for (const auto *url : {"http://example.com:8765/", "http://192.168.31.150.example.com:8765/",
                         "http://speaker.local.evil:8765/", "http://172.32.1.2:8765/",
                         "http://user@speaker.local:8765/", "http://speaker.local:0/",
                         "http://speaker.local:65536/", "http://speaker.local:abc/",
                         "http://speaker.local:/", "http://10.1.2.3.nip.io:8765/"})
    assert(!network_transport_security_allows(url, NetworkTransportSecurity::AllowPlaintextLocal, true));

  std::cout << "http transport policy: default TLS, original development policy, explicit local ports and public rejection passed\n";
  return 0;
}
