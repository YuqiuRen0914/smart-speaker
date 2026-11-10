#pragma once
#include "lunaros_text_raster.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <new>
#include <string>

namespace lunaredu::lunaros::text_raster {
// A drawing-owner cache of exact text coverage, independent of destination
// pixels, color and AA policy. Replay still blends onto the current frame.
// The caller supplies memory (PSRAM on constrained products); allocation
// failure or oversize input falls back to ordinary drawing without lost text.
class TextCoverageCache {
 public:
  struct Memory { void *(*allocate)(size_t)=nullptr; void (*release)(void *)=nullptr; };
  // Synchronous nonempty horizontal runs in original glyph paint order.
  // Coverage is borrowed only for the callback; blending/clipping stay with
  // the caller. Runs do not combine overlapping glyphs or their alpha values.
  using Span = void (*)(void *,int,int,const uint8_t *,size_t);
  TextCoverageCache()=default;
  TextCoverageCache(const TextCoverageCache &)=delete;
  TextCoverageCache &operator=(const TextCoverageCache &)=delete;
  ~TextCoverageCache(){reset();}
  void begin(Memory memory){
    if(memory.allocate!=memory_.allocate||memory.release!=memory_.release)reset();
    memory_=memory;cursor_=0;
  }
  void reset(){
    if(storage_){storage_->~Storage();std::memset(static_cast<void *>(storage_),0,sizeof(Storage));memory_.release(storage_);storage_=nullptr;}
    attempted_=false;cursor_=0;font_ready_=false;
  }
  size_t bytes()const{return storage_?sizeof(Storage):0;}
  void draw_clipped(int x,int y,int width,const std::string &text,unsigned font,Pixel pixel,void *context,int centered_height=0){
    if(pixel)draw_clipped_impl(x,y,width,text,font,{pixel,nullptr,context},centered_height);
  }
  void draw_clipped_spans(int x,int y,int width,const std::string &text,unsigned font,Span span,void *context,int centered_height=0){
    if(span)draw_clipped_impl(x,y,width,text,font,{nullptr,span,context},centered_height);
  }
 private:
  struct Sink {
    Pixel pixel;Span span;void *context;
    static void emit(void *p,int x,int y,uint8_t alpha){
      const auto &sink=*static_cast<Sink *>(p);
      if(sink.span)sink.span(sink.context,x,y,&alpha,1);
      else sink.pixel(sink.context,x,y,alpha);
    }
  };
  void draw_clipped_impl(int x,int y,int width,const std::string &text,unsigned font,Sink sink,int centered_height){
    if(!font_ready_){
      if(!ready()){draw(x,y,text.c_str(),font,Sink::emit,&sink);return;}
      font_ready_=true;
    }
    const auto slot=cursor_++;
    if(!storage_&&!attempted_){
      attempted_=true;
      if(memory_.allocate&&memory_.release)if(auto *p=memory_.allocate(sizeof(Storage)))storage_=new(p) Storage{};
    }
    Entry *entry=storage_&&slot<storage_->entries.size()&&text.size()<=kInputLimit?&storage_->entries[slot]:nullptr;
    const bool hit=entry&&entry->valid&&entry->font==font&&entry->width==width&&entry->length==text.size()&&
        !std::memcmp(entry->input.data(),text.data(),text.size());
    if(!hit&&entry)entry->bounds_valid=false;
    if(centered_height>0){
      const auto extent=entry&&entry->bounds_valid?entry->bounds:text_raster::bounds(text.c_str(),font);
      if(entry){entry->bounds=extent;entry->bounds_valid=true;}
      x+=(width-extent.width)/2-extent.left;y+=(centered_height-extent.height)/2-extent.top;
    }
    if(hit){
      for(size_t i=0;i<entry->count;++i){
        const auto &run=storage_->runs[entry->offset+i];
        const auto *alpha=storage_->alpha.data()+run.alpha;
        if(sink.span)sink.span(sink.context,x+run.x,y+run.y,alpha,run.count);
        else for(size_t j=0;j<run.count;++j)sink.pixel(sink.context,x+run.x+int(j),y+run.y,alpha[j]);
      }
      return;
    }
    const auto line=clip(text,width,font);
    if(!entry){draw(x,y,line.c_str(),font,Sink::emit,&sink);return;}
    // A changed run invalidates the append arena. Earlier runs were already
    // painted in this frame; they are rebuilt on the next frame as needed.
    const size_t expected=size_t(std::min(36000,std::max(0,width)))*size_t(std::min(36000,std::max(1,line_height(font))));
    if(expected>storage_->alpha.size()-storage_->alpha_used||storage_->used+64>storage_->runs.size()){
      for(auto &e:storage_->entries)e.valid=false;
      storage_->used=storage_->alpha_used=0;
    }
    entry->valid=false;entry->font=font;entry->width=width;entry->length=text.size();
    std::memcpy(entry->input.data(),text.data(),text.size());entry->offset=storage_->used;entry->alpha_offset=storage_->alpha_used;entry->count=0;
    Recording recording{storage_,entry,sink,x,y,false};
    draw(x,y,line.c_str(),font,[](void *p,int px,int py,uint8_t alpha){
      auto &r=*static_cast<Recording *>(p);Sink::emit(&r.sink,px,py,alpha);
      const int dx=px-r.x,dy=py-r.y;
      if(r.failed||r.storage->alpha_used==r.storage->alpha.size()||dx<INT16_MIN||dx>INT16_MAX||dy<INT16_MIN||dy>INT16_MAX){r.failed=true;return;}
      Run *last=r.entry->count?&r.storage->runs[r.storage->used-1]:nullptr;
      if(last&&last->y==dy&&int(last->x)+last->count==dx){++last->count;}
      else {
        if(r.storage->used==r.storage->runs.size()){r.failed=true;return;}
        r.storage->runs[r.storage->used++]={int16_t(dx),int16_t(dy),uint16_t(r.storage->alpha_used),1};++r.entry->count;
      }
      r.storage->alpha[r.storage->alpha_used++]=alpha;
    },&recording);
    entry->valid=!recording.failed;
    if(recording.failed){storage_->used=entry->offset;storage_->alpha_used=entry->alpha_offset;entry->count=0;}
  }
  static constexpr size_t kInputLimit=512;
  struct Run {int16_t x,y;uint16_t alpha,count;};
  struct Entry {std::array<char,kInputLimit> input{};size_t offset=0,alpha_offset=0,count=0,length=0;unsigned font=0;int width=0;bool valid=false,bounds_valid=false;TextBounds bounds;};
  struct Storage {std::array<Entry,24> entries{};std::array<Run,9000> runs{};std::array<uint8_t,36000> alpha{};size_t used=0,alpha_used=0;};
  struct Recording {Storage *storage;Entry *entry;Sink sink;int x,y;bool failed;};
  static std::string clip(const std::string &text,int width,unsigned font){
    if(text_raster::width(text.c_str(),font)<=width)return text;
    auto line=text;const int dots=text_raster::width("…",font);
    while(!line.empty()&&text_raster::width(line.c_str(),font)>width-dots){
      size_t end=line.size()-1;while(end&&(static_cast<unsigned char>(line[end])&0xc0)==0x80)--end;line.resize(end);
    }
    return line+"…";
  }
  Storage *storage_=nullptr;
  Memory memory_;
  size_t cursor_=0;
  bool attempted_=false;
  bool font_ready_=false;
};
}
