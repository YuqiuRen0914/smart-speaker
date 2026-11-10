#include "device_api/device_api_context.h"
#include "device_api/device_api.h"
#include "device_api/device_projection.h"

#include <cstring>
#include <atomic>
#ifdef ESP_PLATFORM
#include <esp_app_desc.h>
#endif

#include "kernel/runtime.h"
#include "kernel/static_mutex.h"

namespace lunaredu::lunaros {
namespace {
constexpr size_t kMaxMetadata = 12;
constexpr size_t kMaxOperations = 32;
void set_error(std::string *error, const char *message) {
  if (error != nullptr) *error = message;
}

bool valid_operation(const DeviceOperationDefinition &value, std::string *error) {
  const auto &model = value.model;
  const auto &projection = value.projection;
  if (!model.id || !*model.id) {
    set_error(error, "invalid_device_operation_id");
    return false;
  }
  if (!device_operation_execution_valid(model)) {
    set_error(error, "device_operation_owner_missing");
    return false;
  }
  const bool bound = projection.json_binding != nullptr;
  if (bound && (!projection.json_binding->invoke || projection.json_provider ||
      projection.json_result_provider || projection.json_command_handler ||
      (projection.handler_kind != DeviceOperationHandlerKind::JsonQuery &&
       projection.handler_kind != DeviceOperationHandlerKind::JsonCommand))) {
    set_error(error, "invalid_device_json_binding");
    return false;
  }
  const bool q = projection.json_provider || projection.json_result_provider ||
      (bound && projection.handler_kind == DeviceOperationHandlerKind::JsonQuery);
  const bool c = projection.json_command_handler != nullptr ||
      (bound && projection.handler_kind == DeviceOperationHandlerKind::JsonCommand);
  const bool b = projection.bulk_handler != nullptr;
  const bool m = projection.media_handler != nullptr;
  if (projection.handler_kind == DeviceOperationHandlerKind::JsonQuery &&
      (projection.method != DeviceOperationMethod::Get || !q || c || b || m ||
       (projection.json_provider && projection.json_result_provider))) {
    set_error(error, "invalid_device_json_query_handler");
    return false;
  }
  if (projection.handler_kind == DeviceOperationHandlerKind::JsonCommand &&
      (!c || q || b || m)) {
    set_error(error, "invalid_device_json_command_handler");
    return false;
  }
  if ((projection.handler_kind == DeviceOperationHandlerKind::Bulk ||
       projection.handler_kind == DeviceOperationHandlerKind::Upload) &&
      (!b || q || c || m)) {
    set_error(error, "invalid_device_bulk_handler");
    return false;
  }
  if (projection.handler_kind == DeviceOperationHandlerKind::Media &&
      (projection.method != DeviceOperationMethod::Get || !m || q || c || b)) {
    set_error(error, "invalid_device_media_handler");
    return false;
  }
  if (static_cast<unsigned>(projection.handler_kind) >
      static_cast<unsigned>(DeviceOperationHandlerKind::Media)) {
    set_error(error, "invalid_device_operation_handler_kind");
    return false;
  }
  return true;
}
bool valid_module(const DeviceModuleProjection &value, std::string *error) {
  if (!value.capability_id || !*value.capability_id || !value.module ||
      !value.module->id || !*value.module->id || !value.module->kind ||
      !*value.module->kind || !value.operations.valid(kMaxOperations)) {
    set_error(error, "invalid_device_module");
    return false;
  }
  for (size_t i = 0; i < value.operations.size(); ++i) {
    if (!valid_operation(value.operations[i], error)) return false;
    for (size_t j = 0; j < i; ++j) {
      if (std::strcmp(value.operations[i].model.id,
                     value.operations[j].model.id) == 0) {
        set_error(error, "duplicate_device_operation_id");
        return false;
      }
    }
  }
  return true;
}
}

struct DeviceProjectionRegistry::Impl {
  explicit Impl(DeviceApiContext &owner)
      : device(owner), host_connection(owner) {}
  DeviceApiContext &device;
  DeviceHostConnectionTracker host_connection;
  const DeviceModuleProjection *modules[kDeviceModuleCapacity] = {};
  size_t module_count = 0;
  bool module_sealed = false;
  DeviceMetadataProjection metadata[kMaxMetadata] = {};
  size_t metadata_count = 0;
  bool metadata_sealed = false;
  uint32_t schema_revision = 0;
  uint64_t declaration_hash = 0;
  mutable StaticMutex mutex;
};

DeviceProjectionRegistry::DeviceProjectionRegistry(DeviceApiContext &device)
    : impl_(new Impl(device)) {
  if (!device.projections_) device.projections_ = this;
}
DeviceProjectionRegistry::~DeviceProjectionRegistry() {
  if (impl_->device.projections_ == this) impl_->device.projections_ = nullptr;
  delete impl_;
}

bool DeviceProjectionRegistry::owns_device() const { return impl_->device.projections_ == this; }

KernelRuntime &DeviceProjectionRegistry::runtime() const { return impl_->device.runtime; }

DeviceApiContext &DeviceProjectionRegistry::device() const { return impl_->device; }

DeviceRuntimeContext &DeviceProjectionRegistry::context() { return impl_->device.model; }

const DeviceRuntimeContext &DeviceProjectionRegistry::context() const {
  return impl_->device.model;
}

// Composition (register, seal, reset) is serialized by the product owner.
// Readers may run concurrently, but must be stopped before reset/destruction.
bool DeviceProjectionRegistry::reset(std::string &error) {
  if (!owns_device()) { error = "device_projection_owner_conflict"; return false; }
  StaticLockGuard lock(impl_->mutex);
  if (!impl_->device.model.reset(error)) return false;
  for (auto &module : impl_->modules) module = nullptr;
  for (auto &metadata : impl_->metadata) metadata = {};
  impl_->module_count = impl_->metadata_count = 0;
  impl_->module_sealed = impl_->metadata_sealed = false;
  impl_->schema_revision = 0;
  impl_->declaration_hash = 0;
  error.clear(); return true;
}

bool DeviceProjectionRegistry::register_projection(
    const DeviceProjectionContribution &value, std::string *error) {
  if (error != nullptr) error->clear();
  if (!owns_device()) { set_error(error, "device_projection_owner_conflict"); return false; }
  if (!value.capability && !value.module && !value.metadata) {
    set_error(error, "device_projection_empty");
    return false;
  }
  if (value.module && !value.capability) {
    set_error(error, "device_projection_capability_missing");
    return false;
  }
  if (value.module && (!value.capability->id || !value.module->capability_id ||
      std::strcmp(value.capability->id, value.module->capability_id) != 0)) {
    set_error(error, "device_projection_capability_mismatch");
    return false;
  }
  if (value.module && !valid_module(*value.module, error)) return false;
  if (value.metadata) {
    const auto &m = *value.metadata;
    if (!m.id || !*m.id || (!m.status_provider && !m.info_provider) ||
        (m.status_provider && (!m.status_section || !*m.status_section)) ||
        (!m.status_provider && m.status_section)) {
      set_error(error, "invalid_device_metadata_contribution");
      return false;
    }
  }
  struct Commit { Impl &state; const DeviceProjectionContribution &value; };
  Commit context{*impl_, value};
  auto commit = [](void *opaque, std::string *failure) {
    auto &transaction = *static_cast<Commit *>(opaque);
    auto &state = transaction.state;
    const auto &item = transaction.value;
    StaticLockGuard lock(state.mutex);
    bool module_exists = false, metadata_exists = false;
    if (item.module) {
      for (size_t i = 0; i < state.module_count; ++i) {
        if (std::strcmp(state.modules[i]->module->id,
                        item.module->module->id) != 0) {
          continue;
        }
        if (state.modules[i] != item.module) {
          set_error(failure, "device_module_id_conflict");
          return false;
        }
        module_exists = true;
      }
      if (!module_exists && state.module_sealed) {
        set_error(failure, "device_projection_registry_sealed");
        return false;
      }
      if (!module_exists && state.module_count == kDeviceModuleCapacity) {
        set_error(failure, "device_module_registry_full");
        return false;
      }
    }
    if (item.metadata) {
      const auto &m = *item.metadata;
      for (size_t i = 0; i < state.metadata_count; ++i) {
        const auto &existing = state.metadata[i];
        if (std::strcmp(existing.id, m.id) == 0) {
          if (existing.status_section != m.status_section ||
              existing.status_provider != m.status_provider || existing.info_provider != m.info_provider) {
            set_error(failure, "device_metadata_id_conflict");
            return false;
          }
          metadata_exists = true;
        } else if (m.status_section && existing.status_section &&
                   std::strcmp(m.status_section, existing.status_section) == 0) {
          set_error(failure, "device_metadata_status_section_conflict");
          return false;
        }
      }
      if (!metadata_exists && state.metadata_sealed) {
        set_error(failure, "device_metadata_index_sealed");
        return false;
      }
      if (!metadata_exists && state.metadata_count == kMaxMetadata) {
        set_error(failure, "device_metadata_registry_full");
        return false;
      }
    }
    // Model commit is the last fallible step. Publish all other indexes only
    // after it succeeds; the capability transaction publishes on return.
    if (item.module && !module_exists) {
      DeviceOperationSpec operations[kMaxOperations]{};
      for (size_t i = 0; i < item.module->operations.size(); ++i)
        operations[i] = item.module->operations[i].model;
      DeviceModuleSpec model{
          item.module->module->id,
          item.module->module->kind,
          item.module->capability_id,
          operations,
          item.module->operations.size()};
      if (!state.device.model.models.register_module(model, failure)) return false;
      state.modules[state.module_count++] = item.module;
    }
    if (item.metadata && !metadata_exists) {
      state.metadata[state.metadata_count++] = *item.metadata;
    }
    return true;
  };
  return value.capability
             ? impl_->device.model.capabilities.commit_contribution(
                   *value.capability, value.capability_active, commit, &context,
                   error)
             : commit(&context, error);
}

bool DeviceProjectionRegistry::seal(std::string *error) {
  if (!owns_device()) { set_error(error, "device_projection_owner_conflict"); return false; }
  if (error != nullptr) error->clear();
  {
    StaticLockGuard lock(impl_->mutex);
    if (!impl_->module_count) {
      set_error(error, "device_module_registry_empty");
      return false;
    }
    if (!impl_->metadata_count) {
      set_error(error, "device_metadata_registry_empty");
      return false;
    }
  }
  // Nonempty modules can only be published with a model and capability.
  if (!impl_->device.model.models.seal(error) ||
      !impl_->device.model.capabilities.seal(error)) {
    return false;
  }
  StaticLockGuard lock(impl_->mutex);
  if (!impl_->module_sealed || !impl_->metadata_sealed) {
    static std::atomic<uint32_t> revisions{0};
    impl_->schema_revision = ++revisions;
    // Installation and business can be different cold-boot compositions of
    // the same ELF. A per-process generation alone is 1 for both and leaves
    // the server with the installer's module list after activation.
    uint64_t hash=14695981039346656037ULL;
    const auto mix=[&](uint8_t byte){hash^=byte;hash*=1099511628211ULL;};
    const auto text=[&](const char *value){if(value)while(*value)mix(uint8_t(*value++));mix(255);};
    for(size_t i=0;i<impl_->module_count;++i){
      const auto &module=*impl_->modules[i];
      text(module.capability_id);text(module.module->id);text(module.module->kind);
      text(module.module->workbench_ui);
      for(size_t j=0;j<module.operations.size();++j){
        const auto &operation=module.operations[j];
        text(operation.model.id);text(operation.model.owner_executor);
        mix(uint8_t(operation.model.remote_access));
        mix(uint8_t(operation.projection.method));mix(uint8_t(operation.projection.handler_kind));
      }
      mix(254);
    }
    impl_->declaration_hash=hash;
  }
  impl_->module_sealed = impl_->metadata_sealed = true;
  return true;
}

uint32_t DeviceProjectionRegistry::schema_revision() const {
  StaticLockGuard lock(impl_->mutex);
  return impl_->schema_revision;
}

std::string DeviceProjectionRegistry::declaration_revision() const {
  static const std::string identity = [] {
    std::string value;
#ifdef ESP_PLATFORM
    constexpr char hex[] = "0123456789abcdef";
    for (uint8_t byte : esp_app_get_description()->app_elf_sha256) {
      value += hex[byte >> 4]; value += hex[byte & 15];
    }
#else
    value = "host";
#endif
    return value;
  }();
  StaticLockGuard lock(impl_->mutex);
  return identity + ":" + std::to_string(impl_->declaration_hash);
}

bool DeviceProjectionRegistry::sealed() const {
  if (!owns_device()) return false;
  bool sealed;
  {
    StaticLockGuard lock(impl_->mutex);
    sealed = impl_->module_sealed && impl_->metadata_sealed;
  }
  return sealed && impl_->device.model.models.sealed() && impl_->device.model.capabilities.sealed();
}
void DeviceProjectionRegistry::fill_status_sections(JsonObject payload) const {
  DeviceMetadataProjection copy[kMaxMetadata]{};
  size_t count = 0;
  {
    StaticLockGuard lock(impl_->mutex);
    count = impl_->metadata_count;
    for (size_t index = 0; index < count; ++index) {
      copy[index] = impl_->metadata[index];
    }
  }
  for (size_t index = 0; index < count; ++index) {
    if (copy[index].status_provider != nullptr) {
      copy[index].status_provider(
          impl_->device,
          payload[copy[index].status_section].to<JsonObject>());
    }
  }
}

void DeviceProjectionRegistry::fill_info_payload(JsonObject payload) const {
  DeviceMetadataProjection copy[kMaxMetadata]{};
  size_t count = 0;
  {
    StaticLockGuard lock(impl_->mutex);
    count = impl_->metadata_count;
    for (size_t index = 0; index < count; ++index) {
      copy[index] = impl_->metadata[index];
    }
  }
  for (size_t index = 0; index < count; ++index) {
    if (copy[index].info_provider != nullptr) {
      copy[index].info_provider(impl_->device, payload);
    }
  }
}

void DeviceProjectionRegistry::visit_modules(
    DeviceModuleProjectionVisitor visitor, void *context) const {
  if (visitor == nullptr) return;
  const DeviceModuleProjection *copy[kDeviceModuleCapacity]{};
  size_t count = 0;
  {
    StaticLockGuard lock(impl_->mutex);
    count = impl_->module_count;
    for (size_t index = 0; index < count; ++index) {
      copy[index] = impl_->modules[index];
    }
  }
  for (size_t index = 0; index < count; ++index) {
    visitor(*copy[index], context);
  }
}

const DeviceOperationProjection *DeviceProjectionRegistry::find_operation(
    const char *module_id, const char *operation_id) const {
  if (module_id == nullptr || operation_id == nullptr) return nullptr;
  StaticLockGuard lock(impl_->mutex);
  for (size_t module_index = 0; module_index < impl_->module_count;
       ++module_index) {
    const auto &module = *impl_->modules[module_index];
    if (std::strcmp(module.module->id, module_id) != 0) continue;
    for (size_t operation_index = 0; operation_index < module.operations.size();
         ++operation_index) {
      if (std::strcmp(module.operations[operation_index].model.id,
                      operation_id) == 0) {
        return &module.operations[operation_index].projection;
      }
    }
  }
  return nullptr;
}

} // namespace lunaredu::lunaros
