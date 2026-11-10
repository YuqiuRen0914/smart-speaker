#include "memory/json_schema_workspace.h"
#include <cstring>
#ifdef ESP_PLATFORM
#include <esp_memory_utils.h>
#endif
namespace lunaredu::lunaros {
std::shared_ptr<const JsonDocument> JsonSchemaWorkspace::find(const char *source){
  if(!source)return {};
  std::lock_guard lock(mutex_);
  for(const auto &entry:entries_)if(entry.document&&entry.source==source){++hits_;return entry.document;}
  return {};
}
std::shared_ptr<const JsonDocument> JsonSchemaWorkspace::remember(const char *source,const JsonDocument &parsed){
  if(!source||std::strlen(source)>8192||parsed.isNull())return {};
  uint64_t generation;
  {
    std::lock_guard lock(mutex_);generation=generation_;
    bool free=false;
    for(const auto &entry:entries_){if(entry.document&&entry.source==source){++hits_;return entry.document;}free|=!entry.document;}
    // Static catalog scans larger than the cache must not evict every hot
    // entry or recompile in a second allocator on every discovery request.
    if(!free||!budget_||saturated_)return {};
  }
  auto document=std::make_shared<JsonDocument>(&allocator_);document->set(parsed);
  if(document->overflowed())return {};
  std::string key(source);
#ifdef ESP_PLATFORM
  if(allocator_.stats().internal_live_bytes||!esp_ptr_external_ram(document.get())||!esp_ptr_external_ram(key.data()))return {};
#endif
  std::lock_guard lock(mutex_);
  if(generation!=generation_)return {};
  Entry *free=nullptr;
  for(auto &entry:entries_){if(entry.document&&entry.source==source){++hits_;return entry.document;}if(!entry.document&&!free)free=&entry;}
  const size_t key_bytes=key.capacity()+1,live=allocator_.stats().live_bytes;
  if(!free||live>budget_||source_bytes_>budget_-live||key_bytes>budget_-live-source_bytes_){saturated_=true;return {};}
  source_bytes_+=key_bytes;free->source.swap(key);free->document=document;return document;
}
void JsonSchemaWorkspace::retire(){
  std::lock_guard lock(mutex_);++generation_;entries_={};source_bytes_=0;saturated_=false;allocator_.retire();
}
JsonSchemaWorkspace::Stats JsonSchemaWorkspace::stats()const {
  std::lock_guard lock(mutex_);size_t count=0;for(const auto &entry:entries_)count+=bool(entry.document);
  return {count,allocator_.stats().live_bytes+source_bytes_,hits_};
}
}
