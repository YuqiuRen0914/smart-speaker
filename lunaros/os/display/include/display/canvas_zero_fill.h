#pragma once
#include <cstddef>
#include <cstdint>

namespace lunaredu::display {
struct CanvasZeroSpan {
  size_t prefix=0,aligned=0,suffix=0;
};
// Only whole cache lines may be discarded. Bytes outside the borrowed canvas
// (including allocator neighbours) must never be invalidated or overwritten.
inline CanvasZeroSpan canvas_zero_span(uintptr_t address,size_t bytes,size_t alignment) {
  if(!alignment||(alignment&(alignment-1))||bytes>UINTPTR_MAX-address)return {};
  const size_t prefix=((alignment-(address&(alignment-1)))&(alignment-1));
  if(prefix>bytes)return {bytes,0,0};
  const size_t aligned=(bytes-prefix)&~(alignment-1);
  return {prefix,aligned,bytes-prefix-aligned};
}
struct CanvasZeroFillStats {
  bool ready=false;
  uint32_t calls=0,failures=0;
  uint64_t bytes=0;
};
// One fixed execution owner, holding an exclusive contiguous canvas lease.
// initialize reserves a bounded zero source and reusable GDMA descriptors;
// clear performs no allocations. Unsupported targets return false untouched.
// A successful return has retired DMA before the renderer may use its pixels.
// Stop the owner before destruction. No task or ISR retains borrowed pixels.
class CanvasZeroFill {
 public:
  CanvasZeroFill()=default;
  ~CanvasZeroFill();
  CanvasZeroFill(const CanvasZeroFill &)=delete;
  CanvasZeroFill &operator=(const CanvasZeroFill &)=delete;
  bool initialize(size_t maximum_bytes);
  bool clear(void *pixels,size_t bytes);
  void release();
  CanvasZeroFillStats snapshot() const {return stats_;}
 private:
  void *state_=nullptr;
  bool attempted_=false;
  CanvasZeroFillStats stats_{};
};
}
