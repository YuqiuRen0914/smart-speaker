#pragma once

#include <algorithm>
#include <array>
#include "lunaros_storage.h"

namespace lunaredu::lunaros::internal {

inline bool resource_file_older(const StorageListEntry &a,
                                const StorageListEntry &b) {
  return a.modified_ms != b.modified_ms ? a.modified_ms < b.modified_ms
                                       : a.path < b.path;
}

// Retain only the oldest bounded batch, regardless of directory size.
struct ResourceEvictionBatch {
  static constexpr size_t kCapacity = 32;
  std::array<StorageListEntry, kCapacity> entries{};
  size_t count = 0;
  void offer(const StorageListEntry &entry) {
    if (entry.is_directory) return;
    if (count < entries.size()) {
      entries[count++] = entry;
      std::push_heap(entries.begin(), entries.begin() + count, resource_file_older);
    } else if (resource_file_older(entry, entries.front())) {
      std::pop_heap(entries.begin(), entries.end(), resource_file_older);
      entries.back() = entry;
      std::push_heap(entries.begin(), entries.end(), resource_file_older);
    }
  }
  void sort() {
    std::sort_heap(entries.begin(), entries.begin() + count, resource_file_older);
  }
};

} // namespace lunaredu::lunaros::internal
