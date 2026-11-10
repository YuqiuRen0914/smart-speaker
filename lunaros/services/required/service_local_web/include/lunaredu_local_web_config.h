#pragma once
#include <cstddef>
#include <cstdint>

namespace lunaredu::lunaros {
// Immutable image bytes or an explicitly named filesystem page. Embedded bytes
// win when both are supplied. Buffers must outlive the HTTP service.
struct DeviceWebPage {
  const uint8_t *data = nullptr;
  size_t size = 0;
  const char *storage_path = nullptr;
};
struct DeviceWebConfig {
  const char *product_name = nullptr;
  // Product-owned full HTML at /app. System root remains the standard portal.
  DeviceWebPage application{};
  DeviceWebPage network{};
  DeviceWebPage binding{};
  DeviceWebPage maintenance{};
  // Static semantic operation names; storage must outlive the HTTP service.
  const char *const *local_operations = nullptr;
  size_t local_operation_count = 0;
  // Optional product safety gate, applied on AP and LAN. Must be thread-safe
  // and nonblocking; it restricts operations but never grants access itself.
  void *operation_gate_context = nullptr;
  bool (*operation_gate)(void *, const char *) = nullptr;
  // Permit the static /app template to load after account binding. It must not
  // contain private device state or credentials; APIs still enforce access.
  // Device API operations still require the account owner's access grant.
  bool bound_static_pages = false;
  // Explicit product opt-in for the factory page and its small LAN-only API
  // allowlist on a bound device. Ordinary device operations still need a grant.
  bool bound_lan_factory_access = false;
  // Optional product-owned name for the local application entry.
  const char *application_name = nullptr;
};
}
