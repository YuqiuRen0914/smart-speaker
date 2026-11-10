#include <cstring>

#include "lunaredu_lunaros/device_api/device_projection.h"
#include "kernel/static_mutex.h"
#include "device_api/projections/device_projection_internal.h"

namespace lunaredu::lunaros {

namespace {

void setup_device_module_index();
bool register_device_module_index(const DeviceModuleProjection &, std::string *);
bool seal_device_module_index(std::string *);
bool device_module_index_sealed();

constexpr size_t kMaximumOperationsPerModule = 32;
const DeviceModuleProjection
    *g_modules[lunaredu::lunaros::kDeviceModuleCapacity] = {};
size_t g_module_count = 0;
bool g_registry_sealed = false;
lunaredu::lunaros::StaticMutex g_registry_mutex;

void set_error(std::string *error, const char *message) {
  if (error != nullptr) {
    *error = message;
  }
}

bool valid_operation(const DeviceOperationDefinition &operation,
                     std::string *error) {
  const auto &model = operation.model;
  const auto &projection = operation.projection;
  if (model.id == nullptr || model.id[0] == '\0') {
    set_error(error, "invalid_device_operation_id");
    return false;
  }
  if (projection.operation_id == nullptr ||
      std::strcmp(model.id, projection.operation_id) != 0) {
    set_error(error, "device_operation_projection_mismatch");
    return false;
  }
  if (!device_operation_execution_valid(model)) {
    set_error(error, "device_operation_owner_missing");
    return false;
  }
  const bool has_json_provider = projection.json_provider != nullptr;
  const bool has_json_result_provider =
      projection.json_result_provider != nullptr;
  const bool has_json_command = projection.json_command_handler != nullptr;
  const bool has_bulk = projection.bulk_handler != nullptr;
  const bool has_media = projection.media_handler != nullptr;
  switch (projection.handler_kind) {
  case DeviceOperationHandlerKind::JsonQuery:
    if (projection.method != DeviceOperationMethod::Get ||
        has_json_provider == has_json_result_provider || has_json_command ||
        has_bulk || has_media) {
      set_error(error, "invalid_device_json_query_handler");
      return false;
    }
    return true;
  case DeviceOperationHandlerKind::JsonCommand:
    if (!has_json_command || has_json_provider || has_json_result_provider ||
        has_bulk || has_media) {
      set_error(error, "invalid_device_json_command_handler");
      return false;
    }
    return true;
  case DeviceOperationHandlerKind::Bulk:
  case DeviceOperationHandlerKind::Upload:
    if (!has_bulk || has_json_provider || has_json_result_provider ||
        has_json_command || has_media) {
      set_error(error, "invalid_device_bulk_handler");
      return false;
    }
    return true;
  case DeviceOperationHandlerKind::Media:
    if (projection.method != DeviceOperationMethod::Get || !has_media ||
        has_json_provider || has_json_result_provider || has_json_command ||
        has_bulk) {
      set_error(error, "invalid_device_media_handler");
      return false;
    }
    return true;
  }
  set_error(error, "invalid_device_operation_handler_kind");
  return false;
}

bool valid_module(const DeviceModuleProjection &descriptor,
                  std::string *error) {
  if (descriptor.capability_id == nullptr ||
      descriptor.capability_id[0] == '\0') {
    set_error(error, "invalid_device_module_capability_id");
    return false;
  }
  if (descriptor.module == nullptr || descriptor.module->id == nullptr ||
      descriptor.module->id[0] == '\0' || descriptor.module->kind == nullptr ||
      descriptor.module->kind[0] == '\0' || descriptor.operations == nullptr ||
      descriptor.operation_count == 0 ||
      descriptor.operation_count > kMaximumOperationsPerModule) {
    set_error(error, "invalid_device_module");
    return false;
  }
  for (size_t index = 0; index < descriptor.operation_count; ++index) {
    const DeviceOperationDefinition &operation = descriptor.operations[index];
    if (!valid_operation(operation, error)) {
      return false;
    }
    for (size_t previous = 0; previous < index; ++previous) {
      if (std::strcmp(descriptor.operations[previous].model.id,
                      operation.model.id) == 0) {
        set_error(error, "duplicate_device_operation_id");
        return false;
      }
    }
  }
  return true;
}

int find_module_locked(const char *module_id) {
  for (size_t index = 0; index < g_module_count; ++index) {
    if (std::strcmp(g_modules[index]->module->id, module_id) == 0) {
      return static_cast<int>(index);
    }
  }
  return -1;
}

bool module_slot_available_locked(const DeviceModuleProjection &descriptor,
                                  std::string *error, bool &already_present) {
  const int existing = find_module_locked(descriptor.module->id);
  if (existing >= 0) {
    already_present = g_modules[existing] == &descriptor;
    if (!already_present) {
      set_error(error, "device_module_id_conflict");
    }
    return already_present;
  }
  already_present = false;
  if (g_registry_sealed) {
    set_error(error, "device_projection_registry_sealed");
    return false;
  }
  if (g_module_count >= lunaredu::lunaros::kDeviceModuleCapacity) {
    set_error(error, "device_module_registry_full");
    return false;
  }
  return true;
}

bool append_module_locked(const DeviceModuleProjection &descriptor,
                          std::string *error) {
  lunaredu::lunaros::DeviceOperationSpec
      operations[kMaximumOperationsPerModule] = {};
  for (size_t index = 0; index < descriptor.operation_count; ++index) {
    operations[index] = descriptor.operations[index].model;
  }
  const lunaredu::lunaros::DeviceModuleSpec model = {
      descriptor.module->id,
      descriptor.module->kind,
      descriptor.capability_id,
      operations,
      descriptor.operation_count,
  };
  if (!lunaredu::lunaros::register_device_model_module(model, error)) {
    return false;
  }
  g_modules[g_module_count] = &descriptor;
  ++g_module_count;
  return true;
}

} // namespace

void setup_device_projection_registry() {
  setup_device_capability_index();
  lunaredu::lunaros::setup_device_model_registry();
  setup_device_module_index();
  setup_device_metadata_index();
}

bool register_device_projection(const DeviceProjectionContribution &contribution,
                              std::string *error) {
  if (error != nullptr) {
    error->clear();
  }
  if (contribution.capability == nullptr && contribution.module == nullptr &&
      contribution.metadata == nullptr) {
    set_error(error, "device_projection_empty");
    return false;
  }
  if (contribution.module != nullptr && contribution.capability == nullptr) {
    set_error(error, "device_projection_capability_missing");
    return false;
  }
  if (contribution.module != nullptr &&
      (contribution.capability->id == nullptr ||
       contribution.module->capability_id == nullptr ||
       std::strcmp(contribution.capability->id,
                   contribution.module->capability_id) != 0)) {
    set_error(error, "device_projection_capability_mismatch");
    return false;
  }
  auto commit_metadata = [](void *opaque, std::string *failure) {
    const auto &value =
        *static_cast<const DeviceProjectionContribution *>(opaque);
    auto module_commit = [](void *raw, std::string *module_error) {
      const auto &item = *static_cast<const DeviceProjectionContribution *>(raw);
      return item.module == nullptr ||
             register_device_module_index(*item.module, module_error);
    };
    return value.metadata == nullptr
               ? module_commit(opaque, failure)
               : register_device_metadata_index(*value.metadata, failure,
                                                module_commit, opaque);
  };
  auto *context = const_cast<DeviceProjectionContribution *>(&contribution);
  if (contribution.capability != nullptr) {
    return register_device_capability_contribution(
        *contribution.capability,
        contribution.capability_active, commit_metadata, context, error);
  }
  return commit_metadata(context, error);
}

bool seal_device_projection_registry(std::string *error) {
  if (!seal_device_module_index(error)) {
    return false;
  }
  if (!seal_device_metadata_index(error)) {
    return false;
  }
  return seal_device_capability_registry(error);
}

bool device_projection_registry_sealed() {
  return device_module_index_sealed() && device_metadata_index_sealed() &&
         device_capability_registry_sealed();
}

namespace {

void setup_device_module_index() {
  lunaredu::lunaros::StaticLockGuard lock(g_registry_mutex);
  std::memset(g_modules, 0, sizeof(g_modules));
  g_module_count = 0;
  g_registry_sealed = false;
}

bool register_device_module_index(const DeviceModuleProjection &descriptor,
                                  std::string *error) {
  if (error != nullptr) {
    error->clear();
  }
  if (!valid_module(descriptor, error)) {
    return false;
  }

  lunaredu::lunaros::StaticLockGuard lock(g_registry_mutex);
  bool already_present = false;
  if (!module_slot_available_locked(descriptor, error, already_present)) {
    return false;
  }
  if (already_present) {
    return true;
  }
  return append_module_locked(descriptor, error);
}

bool seal_device_module_index(std::string *error) {
  if (error != nullptr) {
    error->clear();
  }
  lunaredu::lunaros::StaticLockGuard lock(g_registry_mutex);
  if (g_module_count == 0) {
    set_error(error, "device_module_registry_empty");
    return false;
  }
  if (!lunaredu::lunaros::seal_device_model_registry(error)) {
    return false;
  }
  g_registry_sealed = true;
  return true;
}

bool device_module_index_sealed() {
  lunaredu::lunaros::StaticLockGuard lock(g_registry_mutex);
  return g_registry_sealed &&
         lunaredu::lunaros::device_model_registry_sealed();
}

} // namespace

void visit_device_module_projections(DeviceModuleProjectionVisitor visitor, void *context) {
  if (visitor == nullptr) {
    return;
  }
  const DeviceModuleProjection
      *snapshot[lunaredu::lunaros::kDeviceModuleCapacity] = {};
  size_t count = 0;
  {
    lunaredu::lunaros::StaticLockGuard lock(g_registry_mutex);
    count = g_module_count;
    std::memcpy(snapshot, g_modules,
                count * sizeof(DeviceModuleProjection *));
  }
  for (size_t index = 0; index < count; ++index) {
    visitor(*snapshot[index], context);
  }
}

const DeviceOperationProjection *find_device_operation_projection(
    const char *module_id, const char *operation_id) {
  if (module_id == nullptr || operation_id == nullptr) {
    return nullptr;
  }
  lunaredu::lunaros::StaticLockGuard lock(g_registry_mutex);
  for (size_t module_index = 0; module_index < g_module_count;
       ++module_index) {
    const DeviceModuleProjection &module = *g_modules[module_index];
    if (std::strcmp(module.module->id, module_id) != 0) {
      continue;
    }
    for (size_t operation_index = 0;
         operation_index < module.operation_count; ++operation_index) {
      const DeviceOperationDefinition &operation =
          module.operations[operation_index];
      if (operation.model.id != nullptr &&
          std::strcmp(operation.model.id, operation_id) == 0) {
        return &operation.projection;
      }
    }
    return nullptr;
  }
  return nullptr;
}

} // namespace lunaredu::lunaros
