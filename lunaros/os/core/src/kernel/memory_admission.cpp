#include "kernel/memory_admission.h"

namespace lunaredu::lunaros {
bool MemoryStartGate::acquire(MemoryStartRequirements request,MemoryStartWindow &window,std::string &error) {
    if(window||!sample_){error="memory_start_invalid_window";return false;}
    if(occupied_.exchange(true,std::memory_order_acquire)){error="memory_start_busy";return false;}
    MemoryStartWindow candidate;candidate.gate_=this;
    const auto facts=sample_(context_);
    const char *failure=nullptr;
    if(facts.internal.free_bytes<request.internal_free)failure="memory_start_internal_free";
    else if(facts.internal.largest_free_bytes<request.internal_largest)failure="memory_start_internal_block";
    else if(facts.dma.free_bytes<request.dma_free)failure="memory_start_dma_free";
    else if(facts.dma.largest_free_bytes<request.dma_largest)failure="memory_start_dma_block";
    else if(facts.psram.free_bytes<request.external_free)failure="memory_start_external_free";
    else if(facts.psram.largest_free_bytes<request.external_largest)failure="memory_start_external_block";
    if(failure){error=failure;return false;}
    window=std::move(candidate);error.clear();return true;
  }
void MemoryStartWindow::release(){
  if(auto *gate=std::exchange(gate_,nullptr))gate->occupied_.store(false,std::memory_order_release);
}
}
