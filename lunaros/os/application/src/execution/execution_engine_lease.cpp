#include "app/execution/execution_engine_lease.h"
#include <mutex>

namespace lunaredu::lunaros::apps {
namespace {
std::mutex gate_mutex;
const ExecutionEngineLease *owner = nullptr;
ExecutionEngine active = ExecutionEngine::None;
}
bool ExecutionEngineLease::acquire(ExecutionEngine engine) {
  std::lock_guard lock(gate_mutex);
  if (engine == ExecutionEngine::None) return false;
  if (owner) return owner == this && active == engine;
  owner = this; active = engine; return true;
}
void ExecutionEngineLease::release() {
  std::lock_guard lock(gate_mutex);
  if (owner == this) { owner = nullptr; active = ExecutionEngine::None; }
}
bool ExecutionEngineLease::owned() const {
  std::lock_guard lock(gate_mutex); return owner == this;
}
ExecutionEngine active_execution_engine() {
  std::lock_guard lock(gate_mutex); return active;
}
const char *execution_engine_conflict() {
  return "execution_engine_busy";
}
}
