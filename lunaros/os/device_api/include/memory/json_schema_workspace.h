#pragma once
#include "memory/json_workspace.h"
#include <array>
#include <memory>
#include <string>
namespace lunaredu::lunaros {
// Bounded immutable-schema compilation, not an authority/status/result cache.
// Source bytes are the key, so changed declarations cannot reuse old schemas.
// Loans must finish before this owner dies; retire preserves current loans.
class JsonSchemaWorkspace {
 public:
  explicit JsonSchemaWorkspace(size_t maximum_bytes=32768):budget_(maximum_bytes){}
  std::shared_ptr<const JsonDocument> find(const char *source);
  // parsed must be the successful decoding of source. Clone once into this
  // owner's lifetime; full caches decline admission, avoiding cyclic thrash.
  std::shared_ptr<const JsonDocument> remember(const char *source,const JsonDocument &parsed);
  void retire();
  struct Stats {size_t entries=0,bytes=0,hits=0;};
  Stats stats() const;
 private:
  struct Entry {std::string source;std::shared_ptr<const JsonDocument> document;};
  size_t budget_,source_bytes_=0,hits_=0;
  uint64_t generation_=0;
  bool saturated_=false;
  mutable std::mutex mutex_;
  JsonWorkspace allocator_{0};
  std::array<Entry,8> entries_{};
};
}
