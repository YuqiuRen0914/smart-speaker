#include "lunaredu_lunaros/model/device_model.h"

#include <cstring>

#ifdef ESP_PLATFORM
#include <esp_attr.h>
#define LUNAROS_DEVICE_MODEL_LARGE_BSS EXT_RAM_BSS_ATTR
#else
#define LUNAROS_DEVICE_MODEL_LARGE_BSS
#endif

#include "kernel/static_mutex.h"
namespace lunaredu::lunaros {
namespace {

LUNAROS_DEVICE_MODEL_LARGE_BSS DeviceModuleSpec
    g_modules[kDeviceModuleCapacity] = {};
LUNAROS_DEVICE_MODEL_LARGE_BSS DeviceOperationSpec
    g_operations[kDeviceOperationCapacity] = {};
size_t g_module_count = 0;
size_t g_operation_count = 0;
bool g_sealed = false;
StaticMutex g_mutex;

void set_error(std::string *error, const char *message) {
  if (error != nullptr) {
    *error = message;
  }
}

bool same_text(const char *left, const char *right) {
  if (left == nullptr || right == nullptr) {
    return left == right;
  }
  return std::strcmp(left, right) == 0;
}

bool valid_operation(const DeviceOperationSpec &operation,
                     std::string *error) {
  if (operation.id == nullptr || operation.id[0] == '\0') {
    set_error(error, "invalid_device_operation_id");
    return false;
  }
  if (!device_operation_execution_valid(operation)) {
    set_error(error, "device_operation_owner_missing");
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
    if (!valid_operation(module.operations[index], error)) {
      return false;
    }
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
         left.execution == right.execution &&
         left.mutating == right.mutating &&
         same_text(left.owner_executor, right.owner_executor) &&
         same_text(left.request_schema, right.request_schema) &&
         same_text(left.response_schema, right.response_schema);
}

bool equivalent_module(const DeviceModuleSpec &left,
                       const DeviceModuleSpec &right) {
  if (!same_text(left.id, right.id) || !same_text(left.kind, right.kind) ||
      !same_text(left.capability_id, right.capability_id) ||
      left.operation_count != right.operation_count) {
    return false;
  }
  for (size_t index = 0; index < left.operation_count; ++index) {
    if (!equivalent_operation(left.operations[index],
                              right.operations[index])) {
      return false;
    }
  }
  return true;
}

int find_module_locked(const char *module_id) {
  for (size_t index = 0; index < g_module_count; ++index) {
    if (std::strcmp(g_modules[index].id, module_id) == 0) {
      return static_cast<int>(index);
    }
  }
  return -1;
}

} // namespace

void setup_device_model_registry() {
  StaticLockGuard lock(g_mutex);
  for (DeviceModuleSpec &module : g_modules) {
    module = {};
  }
  for (DeviceOperationSpec &operation : g_operations) {
    operation = {};
  }
  g_module_count = 0;
  g_operation_count = 0;
  g_sealed = false;
}

bool register_device_model_module(const DeviceModuleSpec &module,
                                  std::string *error) {
  if (error != nullptr) {
    error->clear();
  }
  if (!valid_module(module, error)) {
    return false;
  }

  StaticLockGuard lock(g_mutex);
  const int existing = find_module_locked(module.id);
  if (existing >= 0) {
    if (equivalent_module(g_modules[static_cast<size_t>(existing)], module)) {
      return true;
    }
    set_error(error, "device_model_module_id_conflict");
    return false;
  }
  if (g_sealed) {
    set_error(error, "device_model_registry_sealed");
    return false;
  }
  if (g_module_count >= kDeviceModuleCapacity) {
    set_error(error, "device_model_module_registry_full");
    return false;
  }
  if (g_operation_count + module.operation_count >
      kDeviceOperationCapacity) {
    set_error(error, "device_model_operation_registry_full");
    return false;
  }

  const size_t operation_start = g_operation_count;
  for (size_t index = 0; index < module.operation_count; ++index) {
    g_operations[g_operation_count++] = module.operations[index];
  }
  g_modules[g_module_count++] = {
      module.id,
      module.kind,
      module.capability_id,
      &g_operations[operation_start],
      module.operation_count,
  };
  return true;
}

bool seal_device_model_registry(std::string *error) {
  if (error != nullptr) {
    error->clear();
  }
  StaticLockGuard lock(g_mutex);
  if (g_module_count == 0) {
    set_error(error, "device_model_registry_empty");
    return false;
  }
  g_sealed = true;
  return true;
}

bool device_model_registry_sealed() {
  StaticLockGuard lock(g_mutex);
  return g_sealed;
}

void visit_device_model_modules(DeviceModuleSpecVisitor visitor,
                                void *context) {
  if (visitor == nullptr) {
    return;
  }
  const DeviceModuleSpec *snapshot[kDeviceModuleCapacity] = {};
  size_t count = 0;
  {
    StaticLockGuard lock(g_mutex);
    count = g_module_count;
    for (size_t index = 0; index < count; ++index) {
      snapshot[index] = &g_modules[index];
    }
  }
  for (size_t index = 0; index < count; ++index) {
    visitor(*snapshot[index], context);
  }
}

const DeviceModuleSpec *find_device_module_spec(const char *module_id) {
  if (module_id == nullptr) {
    return nullptr;
  }
  StaticLockGuard lock(g_mutex);
  const int module_index = find_module_locked(module_id);
  return module_index < 0
             ? nullptr
             : &g_modules[static_cast<size_t>(module_index)];
}

const DeviceOperationSpec *find_device_operation_spec(
    const char *module_id, const char *operation_id) {
  if (module_id == nullptr || operation_id == nullptr) {
    return nullptr;
  }
  StaticLockGuard lock(g_mutex);
  const int module_index = find_module_locked(module_id);
  if (module_index < 0) {
    return nullptr;
  }
  const DeviceModuleSpec &module =
      g_modules[static_cast<size_t>(module_index)];
  for (size_t index = 0; index < module.operation_count; ++index) {
    if (std::strcmp(module.operations[index].id, operation_id) == 0) {
      return &module.operations[index];
    }
  }
  return nullptr;
}

} // namespace lunaredu::lunaros
