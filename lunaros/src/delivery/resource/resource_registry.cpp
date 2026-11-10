#include "delivery/resource/resource_registry.h"

namespace lunaredu::lunaros::internal {

ResourceEntry *ResourceDeliveryRegistry::find(const std::string &id) {
  for (ResourceEntry &entry : entries_) {
    if (entry.occupied && entry.descriptor.id == id) {
      return &entry;
    }
  }
  return nullptr;
}

const ResourceEntry *ResourceDeliveryRegistry::find(const std::string &id) const {
  for (const ResourceEntry &entry : entries_) {
    if (entry.occupied && entry.descriptor.id == id) {
      return &entry;
    }
  }
  return nullptr;
}

ResourceEntry *ResourceDeliveryRegistry::find_or_allocate(const std::string &id) {
  if (ResourceEntry *existing = find(id); existing != nullptr) {
    return existing;
  }
  if (size_ >= entries_.size()) {
    return nullptr;
  }
  for (ResourceEntry &entry : entries_) {
    if (!entry.occupied) {
      entry = {};
      entry.occupied = true;
      entry.descriptor.id = id;
      ++size_;
      return &entry;
    }
  }
  return nullptr;
}

bool ResourceDeliveryRegistry::erase(const std::string &id) {
  ResourceEntry *entry = find(id);
  if (entry == nullptr) return false;
  *entry = {};
  --size_;
  return true;
}

} // namespace lunaredu::lunaros::internal
