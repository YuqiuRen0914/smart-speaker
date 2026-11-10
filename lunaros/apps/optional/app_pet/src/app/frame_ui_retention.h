#pragma once
#include "lunaredu_pet_frame_region.h"
#include <array>
#include <cstdint>

namespace lunaredu::pet {
// Retain decoration only on surfaces whose owner explicitly preserves pixels.
// This tracks memory contents, not physical presentation or input ACKs.
class FrameUiRetention {
 public:
  struct Plan { bool paint_ui; FrameRegion clear; std::array<FrameRegion,2> overlays{}; bool clear_overlays=false; };
  void reset() { entries_={}; next_=0; }
  Plan prepare(PetFrameBuffer f,uint64_t key,FrameRegion character,bool eligible,
               bool fullscreen=false,std::array<FrameRegion,2> overlays={}) {
    Entry *entry=nullptr;
    for(auto &e:entries_)if(e.pixels==f.pixels){entry=&e;break;}
    if(!entry){entry=&entries_[next_];next_=(next_+1)%entries_.size();}
    const bool reuse=eligible&&entry->valid&&entry->pixels==f.pixels&&entry->key==key&&
      entry->width==f.width&&entry->height==f.height&&entry->stride==f.stride&&entry->antialias==f.antialias&&
      entry->native_rgb565==f.native_rgb565&&entry->fullscreen==fullscreen&&
      entry->character.x==character.x&&entry->character.y==character.y&&
      entry->character.width==character.width&&entry->character.height==character.height;
    const auto previous=entry->previous;
    *entry={f.pixels,key,character,f.width,f.height,f.stride,f.antialias,f.native_rgb565,eligible,fullscreen,{}};
    if(reuse&&fullscreen){
      // All Home UI pixels lie in these two bands. Erase them before drawing
      // the character, then repaint: AA must never blend over stale labels.
      // The middle only contains black and the previous character bounds.
      const int top=std::clamp(overlays[0].y+overlays[0].height,0,f.height);
      const int bottom=std::clamp(overlays[1].y,top,f.height);
      const int y=std::clamp(previous.y,top,bottom);
      const int end=int(std::clamp<int64_t>(int64_t(previous.y)+previous.height,y,bottom));
      // Outside the previous character these middle rows are already black.
      // A full row span can use Display's bounded DMA clear without touching
      // either retained UI band. Padding remains outside the declared width.
      return {true,{0,y,f.width,end-y},overlays,true};
    }
    return {!reuse,reuse?character:FrameRegion{0,0,f.width,f.height}};
  }
  void rendered(PetFrameBuffer f,FrameRegion visible){
    for(auto &e:entries_)if(e.pixels==f.pixels){e.previous=visible;return;}
  }
 private:
  struct Entry {
    uint16_t *pixels=nullptr;
    uint64_t key=0;
    FrameRegion character{};
    int width=0,height=0,stride=0,antialias=0,native_rgb565=0;
    bool valid=false,fullscreen=false;
    FrameRegion previous{};
  };
  std::array<Entry,4> entries_{};
  size_t next_=0;
};
}
