#pragma once
#include <cstdint>

namespace lunaredu::lunaros {
// Boot-lived metadata; the service graph supplies executor ownership. A step is
// bounded cooperative upkeep, never a worker, blocking I/O or a stop substitute.
// Return the next delay in milliseconds (0 means the next owner poll).
// Delays are capped at UINT32_MAX/2 to retain wrap-safe scheduling.
struct ModuleMaintenance {
  const char *owner_service_id = nullptr;
  void *context = nullptr;
  uint32_t (*step)(void *, uint32_t now_ms) = nullptr;
};
}
