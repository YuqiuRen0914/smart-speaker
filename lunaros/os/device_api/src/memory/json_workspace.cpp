#include "memory/json_workspace.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#include <esp_memory_utils.h>
#endif
namespace lunaredu::lunaros {
namespace {
void free_storage(void *pointer){
#ifdef ESP_PLATFORM
  heap_caps_free(pointer);
#else
  std::free(pointer);
#endif
}
}

struct alignas(std::max_align_t) JsonWorkspace::Block {
  Block *next=nullptr;
  void *storage=nullptr;
  size_t bytes=0;
  uint64_t generation=0;
  bool cacheable=false;
};
JsonWorkspace::JsonWorkspace(size_t budget):budget_(budget){}
JsonWorkspace::~JsonWorkspace(){retire();}
void *JsonWorkspace::allocate(size_t bytes){
  constexpr size_t overhead=sizeof(Block)+alignof(Block)-1;
  if(!bytes||bytes>std::numeric_limits<size_t>::max()-overhead)return nullptr;
  uint64_t generation;
  {
    std::lock_guard lock(mutex_);generation=generation_;
    Block **best=nullptr;
    for(auto **p=&cached_;*p;p=&(*p)->next)
      if((*p)->bytes>=bytes && (*p)->bytes-bytes<=std::max(bytes,size_t(64)) &&
          (!best||(*p)->bytes<(*best)->bytes))best=p;
    if(best){auto *block=*best;*best=block->next;block->next=nullptr;
      --stats_.cached_blocks;stats_.cached_bytes-=overhead+block->bytes;
      stats_.live_bytes+=overhead+block->bytes;++stats_.hits;
      return block+1;}
  }
  const auto physical=overhead+bytes;
#ifdef ESP_PLATFORM
  const bool external_available=heap_caps_get_total_size(MALLOC_CAP_SPIRAM)>0;
  void *storage=external_available?heap_caps_malloc_prefer(physical,2,
    MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT):
    heap_caps_malloc_prefer(physical,1,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
  // Let the SDK try the preferred heap directly. Its largest-block query
  // walks the whole heap under its allocator lock; it is not an admission
  // check to repeat for every JSON string or page allocation.
  if(external_available&&(!storage||!esp_ptr_external_ram(storage))){
    bool reclaimable;
    {std::lock_guard lock(mutex_);reclaimable=cached_!=nullptr;}
    if(reclaimable){
      retire();{std::lock_guard lock(mutex_);generation=generation_;}
      if(storage){
        // The internal fallback remains a valid reserve. After eviction the
        // SDK may move it to PSRAM, or retain it at the same size without a
        // false OOM for a failed preferred-heap attempt.
        if(void *preferred=heap_caps_realloc_prefer(storage,physical,2,
            MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT))storage=preferred;
      }else storage=heap_caps_malloc_prefer(physical,2,
          MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    }
  }
  const bool cacheable=storage&&esp_ptr_external_ram(storage);
#else
  void *storage=std::malloc(physical);const bool cacheable=true;
#endif
  if(!storage)return nullptr;
  const auto aligned=(reinterpret_cast<uintptr_t>(storage)+alignof(Block)-1)&~uintptr_t(alignof(Block)-1);
  auto *block=new(reinterpret_cast<void *>(aligned)) Block{nullptr,storage,bytes,generation,cacheable};
  {std::lock_guard lock(mutex_);stats_.live_bytes+=physical;if(!cacheable)stats_.internal_live_bytes+=physical;++stats_.allocations;}
  return block+1;
}
void JsonWorkspace::deallocate(void *pointer){
  if(!pointer)return;
  auto *block=static_cast<Block *>(pointer)-1;
  const auto physical=sizeof(Block)+alignof(Block)-1+block->bytes;
  // Do not carry a previous request's parsed strings/variants into retained
  // storage. This is outside the owner metadata lock.
  std::memset(pointer,0,block->bytes);
  {std::lock_guard lock(mutex_);stats_.live_bytes-=physical;if(!block->cacheable)stats_.internal_live_bytes-=physical;
    if(block->cacheable&&block->generation==generation_&&stats_.cached_blocks<32&&
        physical<=budget_-std::min(budget_,stats_.cached_bytes)){
      block->next=cached_;cached_=block;++stats_.cached_blocks;stats_.cached_bytes+=physical;return;
    }}
  free_storage(block->storage);
}
void *JsonWorkspace::reallocate(void *pointer,size_t bytes){
  if(!pointer)return allocate(bytes);
  if(!bytes){deallocate(pointer);return nullptr;}
  auto *block=static_cast<Block *>(pointer)-1;
  if(bytes<=block->bytes)return pointer;
  void *next=allocate(bytes);if(!next)return nullptr;
  std::memcpy(next,pointer,block->bytes);deallocate(pointer);return next;
}
void JsonWorkspace::retire(){
  Block *blocks;
  {std::lock_guard lock(mutex_);++generation_;blocks=cached_;cached_=nullptr;
    stats_.cached_bytes=stats_.cached_blocks=0;}
  while(blocks){auto *next=blocks->next;free_storage(blocks->storage);blocks=next;}
}
JsonWorkspace::Stats JsonWorkspace::stats()const {std::lock_guard lock(mutex_);return stats_;}
}
