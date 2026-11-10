#pragma once
#include "kernel/thread_observation.h"
namespace lunaredu::lunaros {
// Resident CPU monitor keeps its existing 64-record budget. Explicit diagnostic
// reads may borrow up to 96 records through the public span API.
struct NativeThreadCounterRegistrySnapshot {
  bool available=false,complete=false,core_affinity_available=false;
  size_t observed_threads=0,captured_threads=0;
  uint32_t runtime_counter_bits=0;
  std::string error;
  std::array<NativeThreadCounterSnapshot,kNativeThreadSnapshotCapacity> threads{};
};
void capture_native_thread_counters(NativeThreadCounterRegistrySnapshot &);
// One bounded retry through SDK inventory; used only by the monitor fallback.
void capture_native_thread_counters_fallback(NativeThreadCounterRegistrySnapshot &);
}
