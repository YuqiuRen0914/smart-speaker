#include <cassert>
#include <iostream>
#include <string>

#include "delivery/resource/resource_registry.h"

using lunaredu::lunaros::internal::ResourceEntry;
using lunaredu::lunaros::internal::ResourceDeliveryRegistry;

int main() {
  ResourceDeliveryRegistry::Entries entries{};
  ResourceDeliveryRegistry registry(entries);
  ResourceEntry *first = registry.find_or_allocate("resource-0");
  assert(first != nullptr);
  first->descriptor.revision = "v1";
  assert(registry.find("resource-0") == first);
  assert(registry.find_or_allocate("resource-0") == first);
  assert(registry.size() == 1);

  for (size_t index = 1;
       index < lunaredu::lunaros::kResourceRegistryCapacity; ++index) {
    ResourceEntry *entry =
        registry.find_or_allocate("resource-" + std::to_string(index));
    assert(entry != nullptr);
  }
  assert(registry.size() == lunaredu::lunaros::kResourceRegistryCapacity);
  assert(registry.find_or_allocate("overflow") == nullptr);
  assert(registry.find("missing") == nullptr);
  assert(registry.find("resource-0") == first);
  assert(first->descriptor.revision == "v1");

  assert(registry.erase("resource-0"));
  assert(!registry.erase("resource-0"));
  assert(registry.find("resource-0") == nullptr);
  assert(registry.size() == lunaredu::lunaros::kResourceRegistryCapacity - 1);
  assert(registry.find_or_allocate("replacement") != nullptr);
  assert(registry.size() == lunaredu::lunaros::kResourceRegistryCapacity);

  std::cout << "resource registry tests passed: 12\n";
  return 0;
}
