#pragma once

#include <stddef.h>
#include <stdint.h>

#include <string>

namespace lunaredu::lunaros {

inline constexpr size_t kDeviceCapabilityCapacity = 64;

struct DeviceCapabilityDescriptor {
  const char *id = nullptr;
  const char *const *presence_capabilities = nullptr;
  size_t presence_capability_count = 0;
};

using DeviceCapabilityVisitor =
    void (*)(const DeviceCapabilityDescriptor &descriptor, void *context);

// Transport-neutral meaning of a device operation. HTTP verbs, JSON codecs,
// status codes and request-body readers are projections and must not leak into
// this model.
enum class DeviceOperationKind : uint8_t {
  Query,
  Command,
  ResourceRead,
  ResourceWrite,
  Upload,
  Stream,
};

constexpr const char *device_operation_kind_name(DeviceOperationKind kind) {
  switch (kind) {
  case DeviceOperationKind::Query:
    return "query";
  case DeviceOperationKind::Command:
    return "command";
  case DeviceOperationKind::ResourceRead:
    return "resource_read";
  case DeviceOperationKind::ResourceWrite:
    return "resource_write";
  case DeviceOperationKind::Upload:
    return "upload";
  case DeviceOperationKind::Stream:
    return "stream";
  }
  return "query";
}

enum class DeviceOperationExecution : uint8_t {
  OwnerExecutor,
  // The implementation synchronizes its own state. Blocking transport work
  // stays on the invoking task and must not monopolize a control executor.
  Caller,
};

struct DeviceOperationSpec {
  const char *id = nullptr;
  DeviceOperationKind kind = DeviceOperationKind::Query;
  bool mutating = false;
  // OwnerExecutor requires a LunarOS executor ID. Caller requires nullptr
  // and an internally synchronized implementation, independent of transport.
  const char *owner_executor = nullptr;
  const char *request_schema = nullptr;
  const char *response_schema = nullptr;
  DeviceOperationExecution execution = DeviceOperationExecution::OwnerExecutor;
};

constexpr bool device_operation_execution_valid(const DeviceOperationSpec &op) {
  return op.execution == DeviceOperationExecution::Caller
      ? op.owner_executor == nullptr
      : op.execution == DeviceOperationExecution::OwnerExecutor &&
        op.owner_executor != nullptr && op.owner_executor[0] != '\0';
}

struct DeviceModuleSpec {
  const char *id = nullptr;
  const char *kind = nullptr;
  const char *capability_id = nullptr;
  const DeviceOperationSpec *operations = nullptr;
  size_t operation_count = 0;
};

inline constexpr size_t kDeviceModuleCapacity = 48;
inline constexpr size_t kDeviceOperationCapacity = 192;

using DeviceModuleSpecVisitor =
    void (*)(const DeviceModuleSpec &module, void *context);

// The model index is immutable after sealing and safe to consume from UI,
// serial, local HTTP or future USB projections.
void visit_device_model_modules(DeviceModuleSpecVisitor visitor,
                                void *context);
const DeviceModuleSpec *find_device_module_spec(const char *module_id);
const DeviceOperationSpec *find_device_operation_spec(
    const char *module_id, const char *operation_id);

void setup_device_model_registry();
bool register_device_model_module(const DeviceModuleSpec &module,
                                  std::string *error = nullptr);
bool seal_device_model_registry(std::string *error = nullptr);
bool device_model_registry_sealed();

// Availability is a model fact, independent of the transport used to observe
// it. Static capabilities are installed before sealing; runtime readiness may
// activate or deactivate an already declared capability afterwards.
bool declare_device_capability(const DeviceCapabilityDescriptor &descriptor,
                               std::string *error = nullptr);
// Composition transaction: reserve the declaration, commit dependent model or
// projection data, then publish the declaration/activation. A failed callback
// leaves the capability unchanged. The callback must not re-enter this index.
using DeviceCapabilityCommit = bool (*)(void *, std::string *);
bool register_device_capability_contribution(
    const DeviceCapabilityDescriptor &descriptor, bool active,
    DeviceCapabilityCommit commit, void *context, std::string *error);
bool seal_device_capability_registry(std::string *error = nullptr);
bool device_capability_registry_sealed();
bool device_capability_declared(const char *id);
bool activate_device_capability(const DeviceCapabilityDescriptor &descriptor,
                                std::string *error = nullptr);
bool deactivate_device_capability(const char *id);
bool device_capability_active(const char *id);
void visit_device_capabilities(DeviceCapabilityVisitor visitor,
                               void *context);

} // namespace lunaredu::lunaros
