#pragma once

#include <array>
#include <cstddef>
#include <string>

#include "lunaredu_lunaros/network/network_jobs.h"
#include "lunaredu_lunaros/delivery/resources.h"

namespace lunaredu::lunaros::internal {

struct ResourceEntry {
  bool occupied = false;
  ResourceDescriptor descriptor;
  ResourceSnapshot snapshot;
  int priority = 0;
  uint8_t failure_count = 0;
  uint32_t retry_after_ms = 0;
  NetworkJobHandle job;
  bool release_pending = false;
  bool cache_invalid = false;
};

class ResourceDeliveryRegistry {
public:
  using Entries = std::array<ResourceEntry, kResourceRegistryCapacity>;
  // Lifetime belongs to the runtime, keeping slots stable without requiring
  // the mutex and the large record array to occupy the same memory region.
  explicit ResourceDeliveryRegistry(Entries &entries) : entries_(entries) {}
  ResourceDeliveryRegistry(const ResourceDeliveryRegistry &) = delete;
  ResourceDeliveryRegistry &operator=(const ResourceDeliveryRegistry &) = delete;
  ResourceEntry *find(const std::string &id);
  const ResourceEntry *find(const std::string &id) const;
  ResourceEntry *find_or_allocate(const std::string &id);
  bool erase(const std::string &id);
  size_t size() const { return size_; }

  std::array<ResourceEntry, kResourceRegistryCapacity> &entries() {
    return entries_;
  }
  const std::array<ResourceEntry, kResourceRegistryCapacity> &entries() const {
    return entries_;
  }

private:
  Entries &entries_;
  size_t size_ = 0;
};

} // namespace lunaredu::lunaros::internal
