#pragma once

#include <array>
#include <span>
#include <cstdint>
#include <cstddef>
#include <string>

namespace lunaredu::lunaros {
inline constexpr size_t kNativeThreadSnapshotCapacity = 64;
struct NativeThreadSnapshot {
  std::array<char, 32> name{};
  uintptr_t handle = 0;
  uint32_t number = 0, priority = 0, minimum_free_stack_bytes = 0;
  // Address span ending at the SDK's aligned high address, not malloc size.
  uint32_t stack_span_bytes = 0;
  uintptr_t stack_start = 0;
  const char *stack_memory = "unknown";
  int32_t core_affinity = -1;
  uint64_t runtime_counter = 0;
  int32_t idle_core = -1;
  const char *state = "unknown";
};
struct NativeThreadRegistrySnapshot {
  bool available = false, complete = false, core_affinity_available = false;
  size_t observed_threads = 0, captured_threads = 0;
  uint32_t runtime_counter_bits = 0;
  std::string error;
  std::array<NativeThreadSnapshot, kNativeThreadSnapshotCapacity> threads{};
};
// On-demand bounded diagnostic; no worker and no continuous CPU profiling.
void capture_native_threads(NativeThreadRegistrySnapshot &);
// Owned counter facts. No SDK pointers, stack inspection or task allocation.
struct NativeThreadCounterSnapshot {
  std::array<char,32> name{};
  uintptr_t handle=0;
  uint32_t number=0,priority=0;
  int32_t core_affinity=-1,idle_core=-1;
  uint64_t runtime_counter=0;
};
struct NativeThreadCounterFacts {
  bool available=false,complete=false,core_affinity_available=false;
  size_t observed_threads=0,captured_threads=0;
  uint32_t runtime_counter_bits=0;
  uint64_t total_runtime_counter=0;
  bool total_counter_available=false;
  std::string error;
};
inline constexpr size_t kNativeThreadCounterReadCapacity=96;
// Caller supplies bounded storage and owns serialization. Actual kernel list
// protection and copying stay in Core; no borrowed SDK names escape the guard.
// An incomplete read is reported explicitly and is not a CPU usage sample.
NativeThreadCounterFacts capture_native_thread_counters(std::span<NativeThreadCounterSnapshot>);
}

