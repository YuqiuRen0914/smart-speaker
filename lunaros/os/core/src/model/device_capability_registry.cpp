#include "model/device_model.h"

#include <cstring>

#include "kernel/static_mutex.h"

namespace lunaredu::lunaros {
namespace {

void set_error(std::string *error, const char *message) {
  if (error != nullptr) *error = message;
}

bool valid_descriptor(const DeviceCapabilityDescriptor &descriptor,
                      std::string *error) {
  if (descriptor.id == nullptr || descriptor.id[0] == '\0') {
    set_error(error, "invalid_capability_id");
    return false;
  }
  if ((descriptor.presence_capabilities == nullptr) !=
      (descriptor.presence_capability_count == 0)) {
    set_error(error, "invalid_capability_presence_list");
    return false;
  }
  for (size_t index = 0; index < descriptor.presence_capability_count; ++index) {
    if (descriptor.presence_capabilities[index] == nullptr ||
        descriptor.presence_capabilities[index][0] == '\0') {
      set_error(error, "invalid_presence_capability_id");
      return false;
    }
  }
  return true;
}

} // namespace

struct DeviceCapabilityRegistry::Impl {
  Slot capabilities[kDeviceCapabilityCapacity] = {};
  size_t capability_count = 0;
  bool sealed = false;
  uint32_t revision = 0;
  mutable StaticMutex mutex;
};

DeviceCapabilityRegistry::DeviceCapabilityRegistry() : impl_(new Impl()) {}

DeviceCapabilityRegistry::~DeviceCapabilityRegistry() { delete impl_; }

int DeviceCapabilityRegistry::find_locked(const char *id) const {
  for (size_t index = 0; index < impl_->capability_count; ++index) {
    if (impl_->capabilities[index].descriptor != nullptr &&
        std::strcmp(impl_->capabilities[index].descriptor->id, id) == 0) {
      return static_cast<int>(index);
    }
  }
  return -1;
}

bool DeviceCapabilityRegistry::can_declare_locked(
    const DeviceCapabilityDescriptor &descriptor, std::string *error) const {
  const int existing = find_locked(descriptor.id);
  if (existing >= 0) {
    if (impl_->capabilities[static_cast<size_t>(existing)].descriptor == &descriptor) {
      return true;
    }
    set_error(error, "capability_id_conflict");
    return false;
  }
  if (impl_->sealed) {
    set_error(error, "capability_registry_sealed");
    return false;
  }
  if (impl_->capability_count >= kDeviceCapabilityCapacity) {
    set_error(error, "capability_registry_full");
    return false;
  }
  return true;
}

bool DeviceCapabilityRegistry::declare_locked(
    const DeviceCapabilityDescriptor &descriptor, std::string *error) {
  if (!can_declare_locked(descriptor, error)) return false;
  if (find_locked(descriptor.id) < 0) {
    impl_->capabilities[impl_->capability_count++] = {&descriptor, false};
  }
  return true;
}

void DeviceCapabilityRegistry::reset() {
  StaticLockGuard lock(impl_->mutex);
  for (auto &capability : impl_->capabilities) capability = {};
  impl_->capability_count = 0;
  impl_->sealed = false;
  ++impl_->revision;
}

bool DeviceCapabilityRegistry::commit_contribution(
    const DeviceCapabilityDescriptor &descriptor, bool active,
    DeviceCapabilityCommit commit, void *context, std::string *error) {
  if (error != nullptr) error->clear();
  if (!valid_descriptor(descriptor, error)) return false;
  StaticLockGuard lock(impl_->mutex);
  if (!can_declare_locked(descriptor, error)) return false;
  const int existing_slot = find_locked(descriptor.id);
  const bool was_declared = existing_slot >= 0;
  const bool was_active =
      was_declared && impl_->capabilities[static_cast<size_t>(existing_slot)].active;
  if (commit != nullptr && !commit(context, error)) return false;
  (void)declare_locked(descriptor, nullptr);
  if (!was_declared) ++impl_->revision;
  if (active && !was_active) {
    const int slot = find_locked(descriptor.id);
    impl_->capabilities[static_cast<size_t>(slot)].active = true;
    ++impl_->revision;
  }
  return true;
}

bool DeviceCapabilityRegistry::declare_capability(
    const DeviceCapabilityDescriptor &descriptor, std::string *error) {
  if (error != nullptr) error->clear();
  if (!valid_descriptor(descriptor, error)) return false;
  StaticLockGuard lock(impl_->mutex);
  const bool was_declared = find_locked(descriptor.id) >= 0;
  if (!declare_locked(descriptor, error)) return false;
  if (!was_declared) ++impl_->revision;
  return true;
}

bool DeviceCapabilityRegistry::seal(std::string *error) {
  if (error != nullptr) error->clear();
  StaticLockGuard lock(impl_->mutex);
  if (impl_->capability_count == 0) {
    set_error(error, "capability_registry_empty");
    return false;
  }
  impl_->sealed = true;
  return true;
}

bool DeviceCapabilityRegistry::sealed() const {
  StaticLockGuard lock(impl_->mutex);
  return impl_->sealed;
}

bool DeviceCapabilityRegistry::declared(const char *id) const {
  if (id == nullptr || id[0] == '\0') return false;
  StaticLockGuard lock(impl_->mutex);
  return find_locked(id) >= 0;
}

bool DeviceCapabilityRegistry::activate(
    const DeviceCapabilityDescriptor &descriptor, std::string *error) {
  if (error != nullptr) error->clear();
  if (!valid_descriptor(descriptor, error)) return false;
  StaticLockGuard lock(impl_->mutex);
  int slot = find_locked(descriptor.id);
  if (slot < 0) {
    if (impl_->sealed) {
      set_error(error, "capability_not_declared");
      return false;
    }
    if (!declare_locked(descriptor, error)) return false;
    slot = find_locked(descriptor.id);
    ++impl_->revision;
  }
  Slot &capability = impl_->capabilities[static_cast<size_t>(slot)];
  if (capability.descriptor != &descriptor) {
    set_error(error, "capability_id_conflict");
    return false;
  }
  if (!capability.active) {
    capability.active = true;
    ++impl_->revision;
  }
  return true;
}

bool DeviceCapabilityRegistry::deactivate(const char *id) {
  if (id == nullptr || id[0] == '\0') return false;
  StaticLockGuard lock(impl_->mutex);
  const int slot = find_locked(id);
  if (slot < 0) return false;
  if (impl_->capabilities[static_cast<size_t>(slot)].active) {
    impl_->capabilities[static_cast<size_t>(slot)].active = false;
    ++impl_->revision;
  }
  return true;
}

bool DeviceCapabilityRegistry::active(const char *id) const {
  if (id == nullptr || id[0] == '\0') return false;
  StaticLockGuard lock(impl_->mutex);
  const int slot = find_locked(id);
  return slot >= 0 && impl_->capabilities[static_cast<size_t>(slot)].active;
}

uint32_t DeviceCapabilityRegistry::revision() const {
  StaticLockGuard lock(impl_->mutex);
  return impl_->revision;
}

void DeviceCapabilityRegistry::visit(DeviceCapabilityVisitor visitor,
                                     void *context) const {
  if (visitor == nullptr) return;
  const DeviceCapabilityDescriptor *snapshot[kDeviceCapabilityCapacity] = {};
  size_t count = 0;
  {
    StaticLockGuard lock(impl_->mutex);
    for (size_t index = 0; index < impl_->capability_count; ++index) {
      if (impl_->capabilities[index].active) {
        snapshot[count++] = impl_->capabilities[index].descriptor;
      }
    }
  }
  for (size_t index = 0; index < count; ++index) {
    visitor(*snapshot[index], context);
  }
}

} // namespace lunaredu::lunaros
