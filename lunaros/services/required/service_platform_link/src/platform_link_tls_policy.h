#pragma once

#include <cstddef>
#include <cstdint>
#include "lunaros_network_transport_policy.h"

namespace lunaredu::lunaros::platform_link {


enum class PlatformLinkTlsAdmission : uint8_t {
  Direct,
  Maintenance,
  Defer,
};

// PlatformLink requests may share the device with a resident voice session.
// Keep that session intact whenever the current internal heap already has the
// contiguous TLS scratch block. Only interactive recovery may temporarily
// release product-owned resources when the block is unavailable; periodic
// work backs off instead of creating a reconnect loop.
constexpr PlatformLinkTlsAdmission platform_link_tls_admission(
    bool tls_required, size_t internal_largest,
    bool allow_interactive_memory_recovery) {
  if (network_tls_memory_available(tls_required, internal_largest)) {
    return PlatformLinkTlsAdmission::Direct;
  }
  return allow_interactive_memory_recovery
             ? PlatformLinkTlsAdmission::Maintenance
             : PlatformLinkTlsAdmission::Defer;
}

} // namespace lunaredu::lunaros::platform_link
