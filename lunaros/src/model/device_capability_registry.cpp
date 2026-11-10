#include "lunaredu_lunaros/model/device_model.h"

#include <cstring>

#include "kernel/static_mutex.h"

namespace lunaredu::lunaros {

namespace {

struct CapabilitySlot {
  const DeviceCapabilityDescriptor *descriptor = nullptr;
  bool active = false;
};

CapabilitySlot g_capabilities[kDeviceCapabilityCapacity] = {};
size_t g_capability_count = 0;
bool g_registry_sealed = false;
lunaredu::lunaros::StaticMutex g_registry_mutex;

void set_error(std::string *error, const char *message) {
  if (error != nullptr) {
    *error = message;
  }
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
  for (size_t index = 0; index < descriptor.presence_capability_count;
       ++index) {
    if (descriptor.presence_capabilities[index] == nullptr ||
        descriptor.presence_capabilities[index][0] == '\0') {
      set_error(error, "invalid_presence_capability_id");
      return false;
    }
  }
  return true;
}

int find_locked(const char *id) {
  for (size_t index = 0; index < g_capability_count; ++index) {
    if (g_capabilities[index].descriptor != nullptr &&
        std::strcmp(g_capabilities[index].descriptor->id, id) == 0) {
      return static_cast<int>(index);
    }
  }
  return -1;
}

bool can_declare_locked(const DeviceCapabilityDescriptor &descriptor,
                    std::string *error) {
  const int existing = find_locked(descriptor.id);
  if (existing >= 0) {
    if (g_capabilities[static_cast<size_t>(existing)].descriptor ==
        &descriptor) {
      return true;
    }
    set_error(error, "capability_id_conflict");
    return false;
  }
  if (g_registry_sealed) {
    set_error(error, "capability_registry_sealed");
    return false;
  }
  if (g_capability_count >= kDeviceCapabilityCapacity) {
    set_error(error, "capability_registry_full");
    return false;
  }
  return true;
}

bool declare_locked(const DeviceCapabilityDescriptor &descriptor,
                    std::string *error) {
  if (!can_declare_locked(descriptor, error)) return false;
  if (find_locked(descriptor.id) < 0) {
    g_capabilities[g_capability_count++] = {&descriptor, false};
  }
  return true;
}

} // namespace

bool register_device_capability_contribution(
    const DeviceCapabilityDescriptor &descriptor, bool active,
    DeviceCapabilityCommit commit, void *context, std::string *error) {
  if (error != nullptr) error->clear();
  if (!valid_descriptor(descriptor, error)) return false;
  StaticLockGuard lock(g_registry_mutex);
  if (!can_declare_locked(descriptor, error)) return false;
  if (commit != nullptr && !commit(context, error)) return false;
  // The lock preserves the preflight result; publication cannot fail now.
  (void)declare_locked(descriptor, nullptr);
  if (active) g_capabilities[find_locked(descriptor.id)].active = true;
  return true;
}

void setup_device_capability_index() {
  lunaredu::lunaros::StaticLockGuard lock(g_registry_mutex);
  for (CapabilitySlot &slot : g_capabilities) {
    slot = {};
  }
  g_capability_count = 0;
  g_registry_sealed = false;
}

bool declare_device_capability(const DeviceCapabilityDescriptor &descriptor,
                               std::string *error) {
  if (error != nullptr) {
    error->clear();
  }
  if (!valid_descriptor(descriptor, error)) {
    return false;
  }
  lunaredu::lunaros::StaticLockGuard lock(g_registry_mutex);
  return declare_locked(descriptor, error);
}

bool seal_device_capability_registry(std::string *error) {
  if (error != nullptr) {
    error->clear();
  }
  lunaredu::lunaros::StaticLockGuard lock(g_registry_mutex);
  if (g_capability_count == 0) {
    set_error(error, "capability_registry_empty");
    return false;
  }
  g_registry_sealed = true;
  return true;
}

bool device_capability_registry_sealed() {
  lunaredu::lunaros::StaticLockGuard lock(g_registry_mutex);
  return g_registry_sealed;
}

bool device_capability_declared(const char *id) {
  if (id == nullptr || id[0] == '\0') {
    return false;
  }
  lunaredu::lunaros::StaticLockGuard lock(g_registry_mutex);
  return find_locked(id) >= 0;
}

bool activate_device_capability(const DeviceCapabilityDescriptor &descriptor,
                                std::string *error) {
  if (error != nullptr) {
    error->clear();
  }
  if (!valid_descriptor(descriptor, error)) {
    return false;
  }

  lunaredu::lunaros::StaticLockGuard lock(g_registry_mutex);
  int slot = find_locked(descriptor.id);
  if (slot < 0) {
    if (g_registry_sealed) {
      set_error(error, "capability_not_declared");
      return false;
    }
    if (!declare_locked(descriptor, error)) {
      return false;
    }
    slot = find_locked(descriptor.id);
  }
  CapabilitySlot &capability = g_capabilities[static_cast<size_t>(slot)];
  if (capability.descriptor != &descriptor) {
    set_error(error, "capability_id_conflict");
    return false;
  }
  capability.active = true;
  return true;
}

bool deactivate_device_capability(const char *id) {
  if (id == nullptr || id[0] == '\0') {
    return false;
  }

  lunaredu::lunaros::StaticLockGuard lock(g_registry_mutex);
  const int slot = find_locked(id);
  if (slot < 0) {
    return false;
  }
  g_capabilities[static_cast<size_t>(slot)].active = false;
  return true;
}

bool device_capability_active(const char *id) {
  if (id == nullptr || id[0] == '\0') {
    return false;
  }

  lunaredu::lunaros::StaticLockGuard lock(g_registry_mutex);
  const int slot = find_locked(id);
  return slot >= 0 && g_capabilities[static_cast<size_t>(slot)].active;
}

void visit_device_capabilities(DeviceCapabilityVisitor visitor, void *context) {
  if (visitor == nullptr) {
    return;
  }

  const DeviceCapabilityDescriptor *snapshot[kDeviceCapabilityCapacity] = {};
  size_t snapshot_count = 0;
  {
    lunaredu::lunaros::StaticLockGuard lock(g_registry_mutex);
    for (size_t index = 0; index < g_capability_count; ++index) {
      if (g_capabilities[index].active) {
        snapshot[snapshot_count++] = g_capabilities[index].descriptor;
      }
    }
  }

  for (size_t index = 0; index < snapshot_count; ++index) {
    visitor(*snapshot[index], context);
  }
}

} // namespace lunaredu::lunaros
