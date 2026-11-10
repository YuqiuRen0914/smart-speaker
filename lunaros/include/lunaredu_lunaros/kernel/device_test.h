#pragma once

#include <cstdint>

namespace lunaredu::lunaros {

enum class DeviceTestResult {
  Ok, Failed, InvalidArgument, InvalidState, NoMemory, NotSupported, Timeout
};

// Internal wiring for component-owned demos. Applications use the device's
// test.h instead. Definitions and callback state must have static lifetime.
// Callbacks are serialized; they must not call start/stop_device_test.
struct DeviceTestDefinition {
  const char *name;
  DeviceTestResult (*initialize)(const void *config);
  void (*tick)(uint32_t elapsed_ms);
  DeviceTestResult (*shutdown)();
  uint32_t interval_ms = 20;
};

// Config is borrowed only during initialize. A failed initialization is rolled
// back. Duplicate starts are rejected before touching hardware. Up to eight
// demos share one lazily created, budget-accounted worker.
DeviceTestResult start_device_test(const DeviceTestDefinition &test, const void *config);
// Synchronous: no callback can access hardware after a successful stop.
// Failed cleanup stays reserved for a subsequent stop retry. Stop is idempotent.
DeviceTestResult stop_device_test(const DeviceTestDefinition &test);

}  // namespace lunaredu::lunaros
