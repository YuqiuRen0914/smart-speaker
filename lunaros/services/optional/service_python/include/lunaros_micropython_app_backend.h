#pragma once
#include <cstddef>
#include <cstdint>
#include "app/execution/application_backend.h"
namespace lunaredu::micropython {
// Registered by the Python service lifecycle before applications launch.
// This does not initialize the VM or autorun.
bool register_application_backend();
bool validate_application_source(const uint8_t *, size_t);
// Validation remains available when execution is explicitly disabled.
inline bool declare_application_backend() {
  return lunaros::apps::register_application_backend("micropython", nullptr,
      validate_application_source);
}
}
