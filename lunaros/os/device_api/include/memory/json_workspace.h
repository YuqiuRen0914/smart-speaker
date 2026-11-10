#pragma once
#include <ArduinoJson.h>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace lunaredu::lunaros {
// Owner-lifetime allocator, not a document/result cache. Documents must retire
// before the owner dies. Only freed external blocks are retained on ESP.
class JsonWorkspace final : public ArduinoJson::Allocator {
public:
  struct Stats {size_t cached_bytes=0, cached_blocks=0, live_bytes=0, allocations=0, hits=0, internal_live_bytes=0;};
  explicit JsonWorkspace(size_t maximum_cached_bytes=32768);
  ~JsonWorkspace();
  JsonWorkspace(const JsonWorkspace &)=delete;
  JsonWorkspace &operator=(const JsonWorkspace &)=delete;
  void *allocate(size_t) override;
  void deallocate(void *) override;
  void *reallocate(void *,size_t) override;
  // Drop idle storage and fence late deallocations; borrowed documents remain
  // valid and return directly to the heap when their own lifetime ends.
  void retire();
  Stats stats() const;
private:
  struct Block;
  mutable std::mutex mutex_;
  Block *cached_=nullptr;
  size_t budget_;
  uint64_t generation_=1;
  Stats stats_;
};
}
