#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string>

#include <ArduinoJson.h>

#include "lunaredu_lunaros/model/device_model.h"
#include "lunaredu_lunaros/kernel/service_executor.h"

// Device Projection binds the transport-neutral Device Model to the current
// JSON/HTTP surface. HTTP verbs, request readers, response writers and status
// codes belong here and must never leak into device_model.h.

namespace lunaredu::lunaros {

using DeviceStatusProvider = void (*)(JsonObject payload);
using DeviceInfoProvider = void (*)(JsonObject payload);

struct DeviceMetadataProjection {
  const char *id = nullptr;
  const char *status_section = nullptr;
  DeviceStatusProvider status_provider = nullptr;
  DeviceInfoProvider info_provider = nullptr;
};

enum class DeviceOperationMethod : uint8_t {
  Get,
  Post,
  Put,
  Delete,
};

constexpr const char *device_operation_method_name(DeviceOperationMethod method) {
  switch (method) {
  case DeviceOperationMethod::Get:
    return "GET";
  case DeviceOperationMethod::Post:
    return "POST";
  case DeviceOperationMethod::Put:
    return "PUT";
  case DeviceOperationMethod::Delete:
    return "DELETE";
  }
  return "GET";
}

enum class DeviceOperationHandlerKind : uint8_t {
  JsonQuery,
  JsonCommand,
  Bulk,
  Upload,
  Media,
};

constexpr const char *device_operation_handler_kind_name(DeviceOperationHandlerKind kind) {
  switch (kind) {
  case DeviceOperationHandlerKind::JsonQuery:
    return "json_query";
  case DeviceOperationHandlerKind::JsonCommand:
    return "json_command";
  case DeviceOperationHandlerKind::Bulk:
    return "bulk";
  case DeviceOperationHandlerKind::Upload:
    return "upload";
  case DeviceOperationHandlerKind::Media:
    return "media";
  }
  return "json_query";
}

struct DeviceOperationResult {
  bool ok = true;
  uint16_t status_code = 200;
  std::string error;
};

using DeviceJsonProvider = void (*)(JsonObject payload);
using DeviceJsonResultProvider = DeviceOperationResult (*)(JsonObject payload);
using DeviceJsonCommandHandler =
    DeviceOperationResult (*)(JsonVariantConst request_payload,
                       JsonObject response_payload);

struct DeviceBulkRequest {
  DeviceOperationMethod method = DeviceOperationMethod::Get;
  JsonVariantConst payload;
  const uint8_t *body_data = nullptr;
  size_t body_size = 0;
  size_t content_length = 0;
  void *context = nullptr;
  bool (*has_arg)(const char *name, void *context) = nullptr;
  std::string (*arg)(const char *name, void *context) = nullptr;
  int (*read_chunk)(uint8_t *buffer, size_t max_size, void *context) = nullptr;
};

using DeviceBulkHandler =
    DeviceOperationResult (*)(const DeviceBulkRequest &request,
                       JsonObject response_payload);

struct DeviceMediaRequest {
  std::string path;
  int fps = 0;
};

struct DeviceMediaWriter {
  void *context = nullptr;
  bool (*send_binary)(int status_code, const char *content_type,
                      const uint8_t *data, size_t size,
                      void *context) = nullptr;
  bool (*begin_stream)(int status_code, const char *content_type,
                       void *context) = nullptr;
  bool (*send_chunk)(const uint8_t *data, size_t size,
                     void *context) = nullptr;
  void (*end_stream)(void *context) = nullptr;
};

using DeviceMediaHandler =
    DeviceOperationResult (*)(const DeviceMediaRequest &request,
                       const DeviceMediaWriter &writer);

struct DeviceOperationProjection {
  // Stable semantic key used to join this binding with DeviceOperationSpec.
  // All execution ownership and access policy remain in the model.
  const char *operation_id = nullptr;
  DeviceOperationMethod method = DeviceOperationMethod::Get;
  DeviceOperationHandlerKind handler_kind = DeviceOperationHandlerKind::JsonQuery;
  DeviceJsonProvider json_provider = nullptr;
  DeviceJsonResultProvider json_result_provider = nullptr;
  DeviceJsonCommandHandler json_command_handler = nullptr;
  DeviceBulkHandler bulk_handler = nullptr;
  DeviceMediaHandler media_handler = nullptr;
};

// A contributor declares the model and one transport projection together so
// registration can verify their one-to-one join. Consumers receive the two
// views independently after sealing.
struct DeviceOperationDefinition {
  lunaredu::lunaros::DeviceOperationSpec model;
  DeviceOperationProjection projection;
};

// Opt in only for implementations with internal synchronization. The default
// remains owner dispatch; no new task or asynchronous request lifetime exists.
constexpr DeviceOperationDefinition device_operation_on_caller(
    DeviceOperationDefinition definition) {
  definition.model.execution = DeviceOperationExecution::Caller;
  definition.model.owner_executor = nullptr;
  return definition;
}

struct DeviceModuleProjectionMetadata {
  const char *id = nullptr;
  const char *kind = nullptr;
  void (*fill_contract)(JsonObject contract) = nullptr;
};

struct DeviceModuleProjection {
  const char *capability_id = nullptr;
  const DeviceModuleProjectionMetadata *module = nullptr;
  const DeviceOperationDefinition *operations = nullptr;
  size_t operation_count = 0;
};

using DeviceModuleProjectionVisitor =
    void (*)(const DeviceModuleProjection &descriptor, void *context);

struct DeviceProjectionContribution {
  const DeviceCapabilityDescriptor *capability = nullptr;
  const DeviceModuleProjection *module = nullptr;
  const DeviceMetadataProjection *metadata = nullptr;
  bool capability_active = true;
};

void setup_device_projection_registry();
bool register_device_projection(const DeviceProjectionContribution &contribution,
                              std::string *error = nullptr);
bool seal_device_projection_registry(std::string *error = nullptr);
bool device_projection_registry_sealed();

void fill_device_status_sections(JsonObject payload);
void fill_device_info_payload(JsonObject payload);

void visit_device_module_projections(DeviceModuleProjectionVisitor visitor, void *context);
const DeviceOperationProjection *find_device_operation_projection(
    const char *module_id, const char *operation_id);

struct DeviceOperationOptions {
  DeviceOperationMethod method = DeviceOperationMethod::Post;
  bool method_explicit = false;
  const char *owner_executor = lunaredu::lunaros::kMainServiceExecutor;
};

constexpr DeviceOperationOptions device_operation_default_options() { return {}; }

constexpr DeviceOperationOptions device_operation_method(DeviceOperationMethod method) {
  return {method, true, lunaredu::lunaros::kMainServiceExecutor};
}

constexpr DeviceOperationOptions device_operation_on_executor(
    const char *executor,
    DeviceOperationOptions options = device_operation_default_options()) {
  options.owner_executor = executor;
  return options;
}

constexpr DeviceOperationDefinition make_device_json_provider(
    const char *operation_id, DeviceJsonProvider provider,
    const char *owner_executor = lunaredu::lunaros::kMainServiceExecutor) {
  DeviceOperationDefinition definition;
  definition.model.id = operation_id;
  definition.model.kind = lunaredu::lunaros::DeviceOperationKind::Query;
  definition.model.owner_executor = owner_executor;
  definition.projection.operation_id = operation_id;
  definition.projection.method = DeviceOperationMethod::Get;
  definition.projection.handler_kind = DeviceOperationHandlerKind::JsonQuery;
  definition.projection.json_provider = provider;
  return definition;
}

constexpr DeviceOperationDefinition make_device_json_query(
    const char *operation_id, DeviceJsonResultProvider provider,
    const char *owner_executor = lunaredu::lunaros::kMainServiceExecutor) {
  DeviceOperationDefinition definition;
  definition.model.id = operation_id;
  definition.model.kind = lunaredu::lunaros::DeviceOperationKind::Query;
  definition.model.owner_executor = owner_executor;
  definition.projection.operation_id = operation_id;
  definition.projection.method = DeviceOperationMethod::Get;
  definition.projection.handler_kind = DeviceOperationHandlerKind::JsonQuery;
  definition.projection.json_result_provider = provider;
  return definition;
}

constexpr DeviceOperationDefinition make_device_json_command(
    const char *operation_id, DeviceJsonCommandHandler handler,
    DeviceOperationOptions options = device_operation_default_options()) {
  DeviceOperationDefinition definition;
  definition.model.id = operation_id;
  definition.model.kind = lunaredu::lunaros::DeviceOperationKind::Command;
  definition.projection.operation_id = operation_id;
  definition.projection.method =
      options.method_explicit ? options.method : DeviceOperationMethod::Post;
  definition.model.mutating =
      definition.projection.method != DeviceOperationMethod::Get;
  definition.model.owner_executor = options.owner_executor;
  definition.projection.handler_kind =
      DeviceOperationHandlerKind::JsonCommand;
  definition.projection.json_command_handler = handler;
  return definition;
}

constexpr DeviceOperationDefinition make_device_bulk_read(
    const char *operation_id, DeviceBulkHandler handler,
    const char *owner_executor = lunaredu::lunaros::kMainServiceExecutor) {
  DeviceOperationDefinition definition;
  definition.model.id = operation_id;
  definition.model.kind =
      lunaredu::lunaros::DeviceOperationKind::ResourceRead;
  definition.model.owner_executor = owner_executor;
  definition.projection.operation_id = operation_id;
  definition.projection.method = DeviceOperationMethod::Get;
  definition.projection.handler_kind = DeviceOperationHandlerKind::Bulk;
  definition.projection.bulk_handler = handler;
  return definition;
}

constexpr DeviceOperationDefinition make_device_bulk_write(
    const char *operation_id, DeviceBulkHandler handler,
    DeviceOperationOptions options =
        device_operation_method(DeviceOperationMethod::Put)) {
  DeviceOperationDefinition definition;
  definition.model.id = operation_id;
  definition.model.kind =
      lunaredu::lunaros::DeviceOperationKind::ResourceWrite;
  definition.model.mutating = true;
  definition.model.owner_executor = options.owner_executor;
  definition.projection.operation_id = operation_id;
  definition.projection.method =
      options.method_explicit ? options.method : DeviceOperationMethod::Put;
  definition.projection.handler_kind = DeviceOperationHandlerKind::Bulk;
  definition.projection.bulk_handler = handler;
  return definition;
}

constexpr DeviceOperationDefinition make_device_upload(
    const char *operation_id, DeviceBulkHandler handler,
    DeviceOperationOptions options =
        device_operation_method(DeviceOperationMethod::Put)) {
  DeviceOperationDefinition definition =
      make_device_bulk_write(operation_id, handler, options);
  definition.model.kind = lunaredu::lunaros::DeviceOperationKind::Upload;
  definition.projection.handler_kind = DeviceOperationHandlerKind::Upload;
  return definition;
}

constexpr DeviceOperationDefinition make_device_media(
    const char *operation_id, DeviceMediaHandler handler,
    const char *owner_executor = lunaredu::lunaros::kMainServiceExecutor) {
  DeviceOperationDefinition definition;
  definition.model.id = operation_id;
  definition.model.kind = lunaredu::lunaros::DeviceOperationKind::Stream;
  definition.model.owner_executor = owner_executor;
  definition.projection.operation_id = operation_id;
  definition.projection.method = DeviceOperationMethod::Get;
  definition.projection.handler_kind = DeviceOperationHandlerKind::Media;
  definition.projection.media_handler = handler;
  return definition;
}

} // namespace lunaredu::lunaros
