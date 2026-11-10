#pragma once

#include <cstddef>
#include <cstdint>
#include "network/http/network_transport_policy.h"

namespace lunaredu::lunaros::device_link {


enum class DeviceLinkTlsAdmission : uint8_t {
  Direct,
  Maintenance,
  Defer,
};

// DeviceLink requests may share the device with a resident voice session.
// Keep that session intact whenever the current internal heap already has the
// contiguous TLS scratch block. Only interactive recovery may temporarily
// release product-owned resources when the block is unavailable; periodic
// work backs off instead of creating a reconnect loop.
constexpr DeviceLinkTlsAdmission device_link_tls_admission(
    bool production, size_t internal_largest,
    bool allow_interactive_memory_recovery) {
  if (network_tls_memory_available(production, internal_largest)) {
    return DeviceLinkTlsAdmission::Direct;
  }
  return allow_interactive_memory_recovery
             ? DeviceLinkTlsAdmission::Maintenance
             : DeviceLinkTlsAdmission::Defer;
}

} // namespace lunaredu::lunaros::device_link
