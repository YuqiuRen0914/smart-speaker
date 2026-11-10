#pragma once
#include <array>
#include <cstdint>
#include <span>

// Kernel-owned memory facts and bounded failure/checkpoint capture. The
// existing system namespace is retained for the versioned diagnostic schema;
// admission and observation consume the same facts without owning each other.
namespace lunaredu::system {
inline constexpr uint32_t kBootMemoryCheckpointCapacity = 32;
struct MemoryPoolSnapshot {
  uint32_t total_bytes=0, free_bytes=0, minimum_free_bytes=0, largest_free_bytes=0;
};
// Uses the same byte-addressable pool definitions as memory admission.
enum class MemoryPool {Internal,Dma,Psram};
MemoryPoolSnapshot read_memory_pool_snapshot(MemoryPool);
// Periodic observers do not query total capacity, which is not needed for
// free-space history. Keep their three-query cost separate from full inventory.
struct MemoryPoolFreeSnapshot {uint32_t free_bytes=0,minimum_free_bytes=0,largest_free_bytes=0;};
MemoryPoolFreeSnapshot read_memory_pool_free_snapshot(MemoryPool);
struct MemoryFailureSnapshot {
  uint32_t count=0, requested_bytes=0, capabilities=0, uptime_ms=0;
  bool sample_available=false;
  std::array<char,48> allocator{};
};
struct MemoryPressureSnapshot {
  bool failure_capture_available=false;
  MemoryPoolSnapshot internal, dma, psram;
  MemoryFailureSnapshot failure;
};
struct MemoryCheckpoint {
  std::array<char,32> name{};
  uint32_t uptime_ms=0, internal_free_bytes=0, dma_free_bytes=0;
  uint32_t internal_largest_free_bytes=0, psram_free_bytes=0;
};
// Allocation failure callbacks never allocate, log, query heaps or wait for a
// mutex. Concurrent callbacks are sampled with a try-lock; count is captured
// events, not a guarantee that every failed allocation has been observed.
void initialize_memory_pressure_monitor();
MemoryPressureSnapshot read_memory_pressure_snapshot();
void record_boot_memory_checkpoint(const char *name);
bool read_boot_memory_checkpoint(uint32_t index,MemoryCheckpoint &value);

// Optional sampling on an existing task owner. Activity bits describe owner
// demand/residency, not proof of physical I/O or an atomic cross-service view.
inline constexpr uint32_t kMemoryActivityAudio=1,kMemoryActivityDisplay=2,kMemoryActivityMotion=4;
inline constexpr uint32_t kRuntimeMemoryIntervalMs=500,kRuntimeMemoryCapacity=64;
struct RuntimeMemorySample {
  uint64_t sequence=0;
  uint32_t uptime_ms=0,activity_bits=0,allocation_failures=0,capture_us=0;
  MemoryPoolFreeSnapshot internal,dma,psram;
};
struct RuntimeMemoryMinimum {
  uint32_t samples=0,first_ms=0,last_ms=0;
  uint32_t internal_free=0,internal_largest=0,dma_free=0,dma_largest=0,psram_free=0,psram_largest=0;
};
struct RuntimeMemoryHistory {
  bool available=false;
  uint64_t latest_sequence=0,oldest_sequence=0;
  uint32_t retained=0,returned=0,maximum_sample_gap_ms=0,maximum_capture_us=0,skipped_records=0;
  std::array<RuntimeMemoryMinimum,8> activity_minima{};
};
bool runtime_memory_sample_due(uint32_t now_ms);
// No allocation, log, stack scan, heap walk or waiting for an observer lock.
// Heap free/largest queries are rate limited; late owners skip missed periods.
bool capture_runtime_memory_sample(uint32_t now_ms,uint32_t activity_bits);
// Copies newest bounded records in chronological order under a try-lock.
// An unavailable read is explicit; it never fabricates a zero-memory sample.
RuntimeMemoryHistory read_runtime_memory_history(std::span<RuntimeMemorySample> output);
}
