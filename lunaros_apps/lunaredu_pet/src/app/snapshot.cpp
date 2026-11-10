#include "lunaredu_pet_snapshot.h"
#include "runtime/runtime.h"
namespace lunaredu::pet {
using namespace runtime;
PetSnapshot snapshot() {
  std::lock_guard<std::mutex> lock(g_runtime.mutex);
  auto result = g_runtime.snapshot;
  result.pet = g_runtime.pet;
  return result;
}

bool boot_complete() {
  std::lock_guard<std::mutex> lock(g_runtime.mutex);
  return g_runtime.snapshot.boot_complete;
}

void theme_state_into(std::string &state) {
  std::lock_guard<std::mutex> lock(g_runtime.mutex);
  if (g_runtime.snapshot.theme_state.empty()) state = "idle";
  else state = g_runtime.snapshot.theme_state;
}

} // namespace lunaredu::pet
