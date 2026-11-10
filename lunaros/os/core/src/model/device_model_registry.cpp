#include "model/device_model.h"

#include <cstring>
#include <atomic>

#include "kernel/static_mutex.h"
#include "kernel/registration_storage.h"

namespace lunaredu::lunaros {
namespace {

void set_error(std::string *error, const char *message) {
  if (error != nullptr) *error = message;
}

bool same_text(const char *left, const char *right) {
  if (left == nullptr || right == nullptr) return left == right;
  return std::strcmp(left, right) == 0;
}

bool valid_operation(const DeviceOperationSpec &operation, std::string *error) {
  if (operation.id == nullptr || operation.id[0] == '\0') {
    set_error(error, "invalid_device_operation_id");
    return false;
  }
  if (!device_operation_execution_valid(operation)) {
    set_error(error, "device_operation_owner_missing");
    return false;
  }
  if (static_cast<unsigned>(operation.diagnostic_access) >
      static_cast<unsigned>(DeviceDiagnosticAccess::Session)) {
    set_error(error, "invalid_device_diagnostic_access");
    return false;
  }
  if (static_cast<unsigned>(operation.remote_access) > static_cast<unsigned>(DeviceRemoteAccess::Command) ||
      (operation.diagnostic_access != DeviceDiagnosticAccess::None && operation.remote_access != DeviceRemoteAccess::None) ||
      (operation.remote_access == DeviceRemoteAccess::Query &&
       (operation.kind != DeviceOperationKind::Query || operation.mutating)) ||
      (operation.remote_access == DeviceRemoteAccess::Command &&
       (operation.kind != DeviceOperationKind::Command || !operation.mutating))) {
    set_error(error, "invalid_device_remote_access");
    return false;
  }
  return true;
}

bool valid_module(const DeviceModuleSpec &module, std::string *error) {
  if (module.id == nullptr || module.id[0] == '\0' ||
      module.kind == nullptr || module.kind[0] == '\0' ||
      module.capability_id == nullptr || module.capability_id[0] == '\0' ||
      module.operations == nullptr || module.operation_count == 0) {
    set_error(error, "invalid_device_model_module");
    return false;
  }
  for (size_t index = 0; index < module.operation_count; ++index) {
    if (!valid_operation(module.operations[index], error)) return false;
    for (size_t previous = 0; previous < index; ++previous) {
      if (std::strcmp(module.operations[previous].id,
                      module.operations[index].id) == 0) {
        set_error(error, "duplicate_device_operation_id");
        return false;
      }
    }
  }
  return true;
}

bool equivalent_operation(const DeviceOperationSpec &left,
                          const DeviceOperationSpec &right) {
  return same_text(left.id, right.id) && left.kind == right.kind &&
         left.execution == right.execution && left.mutating == right.mutating &&
         left.cancellable == right.cancellable &&
         left.diagnostic_access == right.diagnostic_access &&
         left.remote_access == right.remote_access &&
         same_text(left.owner_executor, right.owner_executor) &&
         same_text(left.request_schema, right.request_schema) &&
         same_text(left.response_schema, right.response_schema);
}

bool equivalent_module(const DeviceModuleSpec &left,
                       const DeviceModuleSpec &right) {
  if (!same_text(left.id, right.id) || !same_text(left.kind, right.kind) ||
      !same_text(left.capability_id, right.capability_id) ||
      left.operation_count != right.operation_count) return false;
  for (size_t index = 0; index < left.operation_count; ++index) {
    if (!equivalent_operation(left.operations[index], right.operations[index])) {
      return false;
    }
  }
  return true;
}

} // namespace

struct DeviceModelRegistry::Impl {
  DeviceModuleSpec modules[kDeviceModuleCapacity] = {};
  RegistrationArray<DeviceOperationSpec> operations[kDeviceModuleCapacity];
  size_t module_count = 0;
  std::atomic<bool> sealed{false};
  mutable StaticMutex mutex;
};

DeviceModelRegistry::DeviceModelRegistry() : impl_(new Impl()) {}

DeviceModelRegistry::~DeviceModelRegistry() { delete impl_; }

int DeviceModelRegistry::find_module_locked(const char *module_id) const {
  for (size_t index = 0; index < impl_->module_count; ++index) {
    if (std::strcmp(impl_->modules[index].id, module_id) == 0) {
      return static_cast<int>(index);
    }
  }
  return -1;
}

void DeviceModelRegistry::reset() {
  StaticLockGuard lock(impl_->mutex);
  for (auto &module : impl_->modules) module = {};
  for (auto &operations : impl_->operations) operations.clear();
  impl_->module_count = 0;
  impl_->sealed = false;
}

bool DeviceModelRegistry::register_module(const DeviceModuleSpec &module,
                                          std::string *error) {
  if (error != nullptr) error->clear();
  if (!valid_module(module, error)) return false;
  StaticLockGuard lock(impl_->mutex);
  const int existing = find_module_locked(module.id);
  if (existing >= 0) {
    if (equivalent_module(impl_->modules[static_cast<size_t>(existing)], module)) {
      return true;
    }
    set_error(error, "device_model_module_id_conflict");
    return false;
  }
  if (impl_->sealed) {
    set_error(error, "device_model_registry_sealed");
    return false;
  }
  if (impl_->module_count >= kDeviceModuleCapacity) {
    set_error(error, "device_model_module_registry_full");
    return false;
  }
  // Trusted boot declarations use the same fallible metadata storage as
  // service registrations. Each module owns an exact-sized stable array;
  // adding another module cannot relocate pointers already handed to readers.
  RegistrationArray<DeviceOperationSpec> operations(true);
  if (!operations.resize(module.operation_count)) {
    set_error(error, "device_model_operation_allocation_failed");
    return false;
  }
  for (size_t index = 0; index < module.operation_count; ++index) {
    operations[index] = module.operations[index];
  }
  const size_t slot = impl_->module_count;
  impl_->operations[slot] = std::move(operations);
  impl_->modules[slot] = {module.id, module.kind,
                               module.capability_id,
                               impl_->operations[slot].data(),
                               module.operation_count};
  ++impl_->module_count;
  return true;
}

bool DeviceModelRegistry::seal(std::string *error) {
  if (error != nullptr) error->clear();
  StaticLockGuard lock(impl_->mutex);
  if (impl_->module_count == 0) {
    set_error(error, "device_model_registry_empty");
    return false;
  }
  impl_->sealed = true;
  return true;
}

bool DeviceModelRegistry::sealed() const {
  return impl_->sealed.load(std::memory_order_acquire);
}

void DeviceModelRegistry::visit(DeviceModuleSpecVisitor visitor,
                                void *context) const {
  if (visitor == nullptr) return;
  DeviceModuleSpec snapshot[kDeviceModuleCapacity] = {};
  size_t count = 0;
  {
    StaticLockGuard lock(impl_->mutex);
    count = impl_->module_count;
    for (size_t index = 0; index < count; ++index) snapshot[index] = impl_->modules[index];
  }
  for (size_t index = 0; index < count; ++index) visitor(snapshot[index], context);
}

const DeviceModuleSpec *DeviceModelRegistry::find_module(
    const char *module_id) const {
  if (module_id == nullptr) return nullptr;
  StaticLockGuard lock(impl_->mutex);
  const int index = find_module_locked(module_id);
  return index < 0 ? nullptr : &impl_->modules[static_cast<size_t>(index)];
}

const DeviceOperationSpec *DeviceModelRegistry::find_operation(
    const char *module_id, const char *operation_id) const {
  if (module_id == nullptr || operation_id == nullptr) return nullptr;
  StaticLockGuard lock(impl_->mutex);
  const int module_index = find_module_locked(module_id);
  if (module_index < 0) return nullptr;
  const DeviceModuleSpec &module = impl_->modules[static_cast<size_t>(module_index)];
  for (size_t index = 0; index < module.operation_count; ++index) {
    if (std::strcmp(module.operations[index].id, operation_id) == 0) {
      return &module.operations[index];
    }
  }
  return nullptr;
}

} // namespace lunaredu::lunaros
