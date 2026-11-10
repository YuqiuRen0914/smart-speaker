#include "lunaredu_pet_snapshot.h"
#include "runtime/runtime.h"
namespace lunaredu::pet {
using namespace runtime;
PetSnapshot snapshot() {
  std::lock_guard<std::mutex> lock(g_runtime.mutex);
  auto result = g_runtime.snapshot;
  result.mood_enabled = g_runtime.policy.mood_enabled;
  result.autonomous_appearance_enabled = g_runtime.policy.autonomous_appearance_enabled;
  return result;
}

PetVisualSnapshot visual_snapshot(uint32_t applied_appearance_revision,
                                  uint32_t applied_expression_revision) {
  std::lock_guard<std::mutex> lock(g_runtime.mutex);
  const auto &state = g_runtime.snapshot;
  PetVisualSnapshot result;
  result.conversation_phase = state.conversation_phase;
  result.boot_complete = state.boot_complete;
  result.expression_revision = state.presentation.expression_revision;
  result.appearance_revision = state.presentation.appearance_revision;
  if (applied_expression_revision != result.expression_revision)
    result.expression_state = state.presentation.expression_state;
  if (applied_appearance_revision != result.appearance_revision) {
    result.shape = state.presentation.shape;
    result.color = state.presentation.color;
  }
  return result;
}

bool boot_complete() {
  std::lock_guard<std::mutex> lock(g_runtime.mutex);
  return g_runtime.snapshot.boot_complete;
}


} // namespace lunaredu::pet
