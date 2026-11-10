#pragma once
#include "kernel/memory_status.h"
#include <atomic>
#include <string>
#include <utility>

namespace lunaredu::lunaros {
// Owner-declared floors for a startup/teardown phase. Internal and DMA pools
// overlap; every constraint is evaluated separately, never summed.
struct MemoryStartRequirements {
  uint32_t internal_free=0,internal_largest=0,dma_free=0,dma_largest=0;
  uint32_t external_free=0,external_largest=0;
};
class MemoryStartGate;
class MemoryStartWindow {
 public:
  MemoryStartWindow()=default;
  ~MemoryStartWindow(){release();}
  MemoryStartWindow(const MemoryStartWindow &)=delete;
  MemoryStartWindow &operator=(const MemoryStartWindow &)=delete;
  MemoryStartWindow(MemoryStartWindow &&other) noexcept:gate_(std::exchange(other.gate_,nullptr)){}
  MemoryStartWindow &operator=(MemoryStartWindow &&other) noexcept {
    if(this!=&other){release();gate_=std::exchange(other.gate_,nullptr);}return *this;
  }
  explicit operator bool()const{return gate_!=nullptr;}
  void release();
 private:
  friend class MemoryStartGate;
  MemoryStartGate *gate_=nullptr;
};
// Serializes participating peak phases, samples AFTER acquiring the gate,
// and never waits on the UI owner. This is not a heap reservation: unrelated
// SDK allocations can still fail and owners must unwind partial startup.
class MemoryStartGate {
 public:
  using Sample=system::MemoryPressureSnapshot (*)(void *);
  explicit MemoryStartGate(Sample sample,void *context=nullptr):sample_(sample),context_(context){}
  bool busy()const{return occupied_.load(std::memory_order_acquire);}
  bool acquire(MemoryStartRequirements request,MemoryStartWindow &window,std::string &error);
 private:
  friend class MemoryStartWindow;
  std::atomic<bool> occupied_{false};
  Sample sample_;void *context_;
};

MemoryStartGate &memory_start_gate();
}
