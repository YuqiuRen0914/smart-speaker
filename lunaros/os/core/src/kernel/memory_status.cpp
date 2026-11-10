#include "kernel/memory_status.h"
#include "kernel/memory_admission.h"
#include "platform/cold_storage.h"
#include <atomic>
#include <algorithm>
#include <limits>
#include <esp_heap_caps.h>
#include <esp_timer.h>

namespace lunaredu::system {
namespace {
std::atomic<uint32_t> capture_state{0};
std::atomic_flag sample_lock=ATOMIC_FLAG_INIT;
std::atomic<uint32_t> failure_count{0};
// Ordinary internal BSS is deliberate. S3's SDK SPIRAM workaround dispatches
// internal 32-bit atomics to hardware; is_always_lock_free is false because
// that same toolchain must also support external addresses. Never cold-place
// this callback counter or its detail lock in PSRAM.
MemoryFailureSnapshot latest;
std::atomic_flag checkpoint_lock=ATOMIC_FLAG_INIT;
LUNAROS_COLD_BSS std::array<MemoryCheckpoint,kBootMemoryCheckpointCapacity> boot_checkpoints{};
uint32_t boot_checkpoint_count=0;
std::atomic_flag history_lock=ATOMIC_FLAG_INIT;
std::atomic_flag history_capture=ATOMIC_FLAG_INIT;
std::atomic<bool> history_attempted{false};
std::atomic<uint32_t> history_attempt_ms{0},history_skipped{0};
struct MemoryHistoryStorage {
  std::array<RuntimeMemorySample,kRuntimeMemoryCapacity> samples{};
  std::array<RuntimeMemoryMinimum,8> minima{};
  uint64_t sequence=0;
  uint32_t last_ms=0,maximum_gap_ms=0,maximum_capture_us=0;
};
// Only trivial zero-initialized payload goes to external NOLOAD BSS. Locks
// and non-trivial C++ owners retain their ordinary internal initialization.
LUNAROS_COLD_BSS constinit MemoryHistoryStorage memory_history{};
uint32_t bounded(size_t value) {
  return static_cast<uint32_t>(std::min(value,size_t(std::numeric_limits<uint32_t>::max())));
}

void failed_allocation(size_t bytes,uint32_t caps,const char *function) {
  failure_count.fetch_add(1,std::memory_order_relaxed);
  if(sample_lock.test_and_set(std::memory_order_acquire))return;
  latest.requested_bytes=bounded(bytes);
  latest.capabilities=caps;
  latest.uptime_ms=static_cast<uint32_t>(esp_timer_get_time()/1000);
  latest.allocator.fill(0);
  if(function)for(size_t i=0;i+1<latest.allocator.size()&&function[i];++i)latest.allocator[i]=function[i];
  latest.sample_available=true;
  sample_lock.clear(std::memory_order_release);
}
MemoryPoolFreeSnapshot pool_free(uint32_t caps){
  return {bounded(heap_caps_get_free_size(caps)),bounded(heap_caps_get_minimum_free_size(caps)),
      bounded(heap_caps_get_largest_free_block(caps))};
}
uint32_t pool_caps(MemoryPool kind){
  switch(kind){
    case MemoryPool::Internal:return MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT;
    case MemoryPool::Dma:return MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA|MALLOC_CAP_8BIT;
    case MemoryPool::Psram:return MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT;
  }
  return 0;
}
}
MemoryPoolFreeSnapshot read_memory_pool_free_snapshot(MemoryPool kind){return pool_free(pool_caps(kind));}
MemoryPoolSnapshot read_memory_pool_snapshot(MemoryPool kind){
  const auto caps=pool_caps(kind);const auto free=pool_free(caps);
  return {bounded(heap_caps_get_total_size(caps)),free.free_bytes,free.minimum_free_bytes,free.largest_free_bytes};
}
void initialize_memory_pressure_monitor() {
  uint32_t expected=0;
  if(!capture_state.compare_exchange_strong(expected,1))return;
  capture_state.store(heap_caps_register_failed_alloc_callback(failed_allocation)==ESP_OK?2:3,std::memory_order_release);
}
MemoryPressureSnapshot read_memory_pressure_snapshot() {
  MemoryPressureSnapshot result;
  result.failure_capture_available=capture_state.load(std::memory_order_acquire)==2;
  result.internal=read_memory_pool_snapshot(MemoryPool::Internal);
  result.dma=read_memory_pool_snapshot(MemoryPool::Dma);
  result.psram=read_memory_pool_snapshot(MemoryPool::Psram);
  if(!sample_lock.test_and_set(std::memory_order_acquire)) {
    result.failure=latest;
    sample_lock.clear(std::memory_order_release);
  }
  result.failure.count=failure_count.load(std::memory_order_relaxed);
  return result;
}
void record_boot_memory_checkpoint(const char *name) {
  MemoryCheckpoint value;
  if(name)for(size_t i=0;i+1<value.name.size()&&name[i];++i)value.name[i]=name[i];
  value.uptime_ms=static_cast<uint32_t>(esp_timer_get_time()/1000);
  value.internal_free_bytes=bounded(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
  value.dma_free_bytes=bounded(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA|MALLOC_CAP_8BIT));
  value.internal_largest_free_bytes=bounded(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
  value.psram_free_bytes=bounded(heap_caps_get_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
  if(checkpoint_lock.test_and_set(std::memory_order_acquire))return;
  if(boot_checkpoint_count<boot_checkpoints.size())boot_checkpoints[boot_checkpoint_count++]=value;
  checkpoint_lock.clear(std::memory_order_release);
}
bool read_boot_memory_checkpoint(uint32_t index,MemoryCheckpoint &value) {
  if(checkpoint_lock.test_and_set(std::memory_order_acquire))return false;
  const bool available=index<boot_checkpoint_count;
  if(available)value=boot_checkpoints[index];
  checkpoint_lock.clear(std::memory_order_release);
  return available;
}
bool runtime_memory_sample_due(uint32_t now) {
  return !history_attempted.load(std::memory_order_acquire)||
      uint32_t(now-history_attempt_ms.load(std::memory_order_relaxed))>=kRuntimeMemoryIntervalMs;
}
bool capture_runtime_memory_sample(uint32_t now,uint32_t activity) {
  if(activity>=memory_history.minima.size()||history_capture.test_and_set(std::memory_order_acquire))return false;
  struct Release {~Release(){history_capture.clear(std::memory_order_release);}} release;
  if(!runtime_memory_sample_due(now))return false;
  // Rate limit attempts too: a contended reader must not turn a dropped
  // record into nine heap queries on every subsequent 5 ms product release.
  history_attempt_ms.store(now,std::memory_order_relaxed);
  history_attempted.store(true,std::memory_order_release);
  const auto start=esp_timer_get_time();
  RuntimeMemorySample sample;
  sample.uptime_ms=now;sample.activity_bits=activity;
  sample.internal=read_memory_pool_free_snapshot(MemoryPool::Internal);
  sample.dma=read_memory_pool_free_snapshot(MemoryPool::Dma);
  sample.psram=read_memory_pool_free_snapshot(MemoryPool::Psram);
  sample.allocation_failures=failure_count.load(std::memory_order_relaxed);
  sample.capture_us=bounded(std::max<int64_t>(0,esp_timer_get_time()-start));
  if(history_lock.test_and_set(std::memory_order_acquire)){
    history_skipped.fetch_add(1,std::memory_order_relaxed);return false;
  }
  auto &h=memory_history;
  if(h.sequence)h.maximum_gap_ms=std::max(h.maximum_gap_ms,uint32_t(now-h.last_ms));
  h.last_ms=now;h.maximum_capture_us=std::max(h.maximum_capture_us,sample.capture_us);
  sample.sequence=++h.sequence;h.samples[(sample.sequence-1)%kRuntimeMemoryCapacity]=sample;
  auto &m=h.minima[activity];
  if(!m.samples) {
    m.first_ms=now;m.internal_free=sample.internal.free_bytes;m.internal_largest=sample.internal.largest_free_bytes;
    m.dma_free=sample.dma.free_bytes;m.dma_largest=sample.dma.largest_free_bytes;
    m.psram_free=sample.psram.free_bytes;m.psram_largest=sample.psram.largest_free_bytes;
  }else{
    m.internal_free=std::min(m.internal_free,sample.internal.free_bytes);
    m.internal_largest=std::min(m.internal_largest,sample.internal.largest_free_bytes);
    m.dma_free=std::min(m.dma_free,sample.dma.free_bytes);m.dma_largest=std::min(m.dma_largest,sample.dma.largest_free_bytes);
    m.psram_free=std::min(m.psram_free,sample.psram.free_bytes);m.psram_largest=std::min(m.psram_largest,sample.psram.largest_free_bytes);
  }
  if(m.samples<UINT32_MAX)++m.samples;
  m.last_ms=now;history_lock.clear(std::memory_order_release);return true;
}
RuntimeMemoryHistory read_runtime_memory_history(std::span<RuntimeMemorySample> output) {
  RuntimeMemoryHistory result;
  if(history_lock.test_and_set(std::memory_order_acquire))return result;
  const auto &h=memory_history;result.available=true;result.latest_sequence=h.sequence;
  result.retained=uint32_t(std::min<uint64_t>(h.sequence,kRuntimeMemoryCapacity));
  result.oldest_sequence=result.retained?h.sequence-result.retained+1:0;
  result.returned=uint32_t(std::min<size_t>(result.retained,output.size()));
  for(uint32_t i=0;i<result.returned;++i)output[i]=h.samples[(h.sequence-result.returned+i)%kRuntimeMemoryCapacity];
  result.maximum_sample_gap_ms=h.maximum_gap_ms;result.maximum_capture_us=h.maximum_capture_us;
  result.skipped_records=history_skipped.load(std::memory_order_relaxed);
  result.activity_minima=h.minima;
  history_lock.clear(std::memory_order_release);return result;
}
}

namespace lunaredu::lunaros {
MemoryStartGate &memory_start_gate(){
  static MemoryStartGate gate([](void *){return system::read_memory_pressure_snapshot();});
  return gate;
}
}
