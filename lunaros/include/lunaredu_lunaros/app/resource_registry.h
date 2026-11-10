#pragma once

#include <array>
#include <stddef.h>
#include <string>

#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif

#ifndef CONFIG_LUNAREDU_LUNAROS_RESOURCE_LEASE_CAPACITY
#define CONFIG_LUNAREDU_LUNAROS_RESOURCE_LEASE_CAPACITY 32
#endif

namespace lunaredu::lunaros {

constexpr size_t kResourceLeaseCapacity =
    CONFIG_LUNAREDU_LUNAROS_RESOURCE_LEASE_CAPACITY;

enum class ResourceAccess {
  Shared,
  Exclusive,
};

constexpr const char *resource_access_name(ResourceAccess access) {
  switch (access) {
  case ResourceAccess::Shared:
    return "shared";
  case ResourceAccess::Exclusive:
    return "exclusive";
  }
  return "exclusive";
}

struct ResourceRequirement {
  const char *id = nullptr;
  ResourceAccess access = ResourceAccess::Exclusive;
};

enum class ResourceLeaseState {
  Reserved,
  Active,
};

constexpr const char *resource_lease_state_name(ResourceLeaseState state) {
  switch (state) {
  case ResourceLeaseState::Reserved:
    return "reserved";
  case ResourceLeaseState::Active:
    return "active";
  }
  return "reserved";
}

struct ResourceLeaseSnapshot {
  std::string resource_id;
  std::string owner_id;
  ResourceAccess access = ResourceAccess::Exclusive;
  ResourceLeaseState state = ResourceLeaseState::Reserved;
};

struct ResourceRegistrySnapshot {
  size_t lease_count = 0;
  std::array<ResourceLeaseSnapshot, kResourceLeaseCapacity> leases;
};

// ResourceRegistry is intentionally single-owner-task state. Other tasks must
// dispatch resource operations to the LunarOS owner task instead of calling it
// concurrently.
class ResourceRegistry {
public:
  bool can_reserve(const std::string &owner_id,
                   const ResourceRequirement *requirements,
                   size_t requirement_count,
                   const char *replacing_owner_id,
                   std::string &error) const;
  bool reserve(const std::string &owner_id,
               const ResourceRequirement *requirements,
               size_t requirement_count,
               const char *replacing_owner_id,
               std::string &error);
  bool commit(const std::string &owner_id, const char *replacing_owner_id,
              std::string &error);
  bool acquire(const std::string &owner_id,
               const ResourceRequirement *requirements,
               size_t requirement_count, std::string &error);
  void cancel(const std::string &owner_id);
  void release(const std::string &owner_id);

  bool owner_holds(const std::string &owner_id,
                   const std::string &resource_id) const;
  ResourceRegistrySnapshot snapshot() const;

private:
  struct Lease {
    std::string resource_id;
    std::string owner_id;
    ResourceAccess access = ResourceAccess::Exclusive;
    ResourceLeaseState state = ResourceLeaseState::Reserved;
  };

  void remove_owner(const std::string &owner_id,
                    bool remove_active, bool remove_reserved);

  std::array<Lease, kResourceLeaseCapacity> leases_;
  size_t lease_count_ = 0;
};

} // namespace lunaredu::lunaros
