#pragma once
#include <cstdint>

namespace lunaredu::lunaros::apps {
enum class ExecutionEngine : uint8_t { None, MicroPython, Wasm };

// One process-wide VM admission slot. Keep the owner alive until every VM
// allocation and worker has exited; failed stop must retain the lease.
// Release is explicit: destroying an object cannot prove a VM has stopped.
class ExecutionEngineLease {
public:
  ExecutionEngineLease() = default;
  ExecutionEngineLease(const ExecutionEngineLease &) = delete;
  ExecutionEngineLease &operator=(const ExecutionEngineLease &) = delete;
  bool acquire(ExecutionEngine engine);
  void release();
  bool owned() const;
};
ExecutionEngine active_execution_engine();
const char *execution_engine_conflict();
}
