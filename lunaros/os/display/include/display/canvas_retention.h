#pragma once
#include "display/frame.h"
#include "display/pipeline/frame_damage.h"
#include <array>

namespace lunaredu::display {
class CanvasRetention {
 public:
  struct Reuse { bool valid=false; Damage overwritten{}; };
  void begin(FrameCanvas canvas,uint64_t page,uint64_t ownership=0) {
    canvas_=canvas;page_=page;ownership_=ownership;previous_={};pending_={};
    if(canvas.slot<entries_.size()) { previous_=entries_[canvas.slot];entries_[canvas.slot]={}; }
  }
  void invalidate() { previous_={};pending_={}; }
  Reuse borrow(const void *owner,uint64_t key,bool eligible) {
    if(!eligible||!owner||!canvas_.pixels||canvas_.slot>=entries_.size()){invalidate();return {};}
    const bool valid=previous_.owner==owner&&previous_.key==key&&previous_.page==page_&&
        previous_.ownership==ownership_&&
        previous_.canvas.issuer==canvas_.issuer&&previous_.canvas.pixels==canvas_.pixels&&
        previous_.canvas.width==canvas_.width&&previous_.canvas.height==canvas_.height&&
        previous_.canvas.stride==canvas_.stride&&previous_.canvas.generation<canvas_.generation;
    pending_={canvas_,owner,key,page_,{},ownership_};
    return {valid,valid?previous_.overwritten:Damage{}};
  }
  void finish(Damage overwritten,bool eligible) {
    if(canvas_.slot<entries_.size()&&eligible&&pending_.owner){pending_.overwritten=overwritten;entries_[canvas_.slot]=pending_;}
    previous_={};pending_={};
  }
 private:
  struct Entry {FrameCanvas canvas;const void *owner=nullptr;uint64_t key=0,page=0;Damage overwritten;uint64_t ownership=0;};
  std::array<Entry,2> entries_{};
  Entry previous_,pending_;
  FrameCanvas canvas_;
  uint64_t page_=0;
  uint64_t ownership_=0;
};
}
