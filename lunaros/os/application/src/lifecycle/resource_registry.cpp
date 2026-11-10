#include "app/lifecycle/app_controller.h"

namespace lunaredu::app {
namespace {

bool resource_conflicts(lunaros::ResourceAccess requested,
                        lunaros::ResourceAccess held) {
  return requested == lunaros::ResourceAccess::Exclusive ||
         held == lunaros::ResourceAccess::Exclusive;
}

bool owner_matches(const std::string &owner_id, const char *candidate) {
  return candidate != nullptr && candidate[0] != '\0' &&
         owner_id == candidate;
}

} // namespace

bool AppController::ResourceRegistry::can_reserve(
    const std::string &owner_id,
    const lunaros::ResourceRequirement *requirements,
    size_t requirement_count, const char *replacing_owner_id,
    std::string &error) const {
  error.clear();
  if (owner_id.empty()) {
    error = "invalid_resource_owner";
    return false;
  }
  if (requirement_count > leases_.size()) {
    error = "resource_requirement_capacity_exceeded";
    return false;
  }
  if (requirement_count > 0 && requirements == nullptr) {
    error = "invalid_resource_requirements";
    return false;
  }

  for (size_t lease_index = 0; lease_index < lease_count_; ++lease_index) {
    if (leases_[lease_index].owner_id == owner_id) {
      error = "resource_owner_already_holds_lease";
      return false;
    }
  }
  if (lease_count_ + requirement_count > leases_.size()) {
    error = "resource_lease_registry_full";
    return false;
  }

  for (size_t index = 0; index < requirement_count; ++index) {
    const lunaros::ResourceRequirement &requirement = requirements[index];
    if (requirement.id == nullptr || requirement.id[0] == '\0') {
      error = "invalid_resource_id";
      return false;
    }
    for (size_t previous = 0; previous < index; ++previous) {
      if (requirements[previous].id != nullptr &&
          std::string(requirements[previous].id) == requirement.id) {
        error = "duplicate_resource_requirement:" +
                std::string(requirement.id);
        return false;
      }
    }

    for (size_t lease_index = 0; lease_index < lease_count_; ++lease_index) {
      const Lease &lease = leases_[lease_index];
      if (lease.resource_id != requirement.id) {
        continue;
      }
      if (lease.state == ResourceLeaseState::Active &&
          owner_matches(lease.owner_id, replacing_owner_id)) {
        continue;
      }
      if (!resource_conflicts(requirement.access, lease.access)) {
        continue;
      }
      error = "resource_conflict:" + std::string(requirement.id) + ":" +
              lease.owner_id;
      return false;
    }
  }
  return true;
}

bool AppController::ResourceRegistry::reserve(
    const std::string &owner_id,
    const lunaros::ResourceRequirement *requirements,
    size_t requirement_count, const char *replacing_owner_id,
    std::string &error) {
  if (!can_reserve(owner_id, requirements, requirement_count,
                   replacing_owner_id, error)) {
    return false;
  }
  for (size_t index = 0; index < requirement_count; ++index) {
    Lease &lease = leases_[lease_count_++];
    lease.resource_id = requirements[index].id;
    lease.owner_id = owner_id;
    lease.access = requirements[index].access;
    lease.state = ResourceLeaseState::Reserved;
  }
  return true;
}

bool AppController::ResourceRegistry::commit(
    const std::string &owner_id, const char *replacing_owner_id,
    std::string &error) {
  error.clear();
  if (owner_id.empty()) {
    error = "invalid_resource_owner";
    return false;
  }

  if (replacing_owner_id != nullptr && replacing_owner_id[0] != '\0' &&
      owner_id != replacing_owner_id) {
    remove_owner(replacing_owner_id, true, false);
  }
  for (size_t index = 0; index < lease_count_; ++index) {
    if (leases_[index].owner_id == owner_id &&
        leases_[index].state == ResourceLeaseState::Reserved) {
      leases_[index].state = ResourceLeaseState::Active;
    }
  }
  return true;
}

bool AppController::ResourceRegistry::acquire(
    const std::string &owner_id,
    const lunaros::ResourceRequirement *requirements,
    size_t requirement_count, std::string &error) {
  if (!reserve(owner_id, requirements, requirement_count, nullptr, error)) {
    return false;
  }
  if (commit(owner_id, nullptr, error)) {
    return true;
  }
  cancel(owner_id);
  return false;
}

void AppController::ResourceRegistry::cancel(const std::string &owner_id) {
  remove_owner(owner_id, false, true);
}

void AppController::ResourceRegistry::release(const std::string &owner_id) {
  remove_owner(owner_id, true, true);
}

bool AppController::ResourceRegistry::owner_holds(
    const std::string &owner_id, const std::string &resource_id) const {
  for (size_t index = 0; index < lease_count_; ++index) {
    const Lease &lease = leases_[index];
    if (lease.owner_id == owner_id && lease.resource_id == resource_id &&
        lease.state == ResourceLeaseState::Active) {
      return true;
    }
  }
  return false;
}

void AppController::ResourceRegistry::remove_owner(
    const std::string &owner_id, bool remove_active, bool remove_reserved) {
  size_t write_index = 0;
  for (size_t read_index = 0; read_index < lease_count_; ++read_index) {
    const Lease &lease = leases_[read_index];
    const bool remove =
        lease.owner_id == owner_id &&
        ((remove_active && lease.state == ResourceLeaseState::Active) ||
         (remove_reserved && lease.state == ResourceLeaseState::Reserved));
    if (!remove) {
      if (write_index != read_index) {
        leases_[write_index] = lease;
      }
      ++write_index;
    }
  }
  for (size_t index = write_index; index < lease_count_; ++index) {
    leases_[index] = {};
  }
  lease_count_ = write_index;
}

} // namespace lunaredu::app
