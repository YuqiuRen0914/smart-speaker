#pragma once
#include "device_api/device_operation.h"
#include "kernel/service_executor.h"

namespace lunaredu::lunaros {
constexpr DeviceOperationDefinition device_operation_composable(DeviceOperationDefinition definition) {
  definition.projection.composable = true;
  return definition;
}
constexpr DeviceOperationDefinition device_operation_diagnostics(DeviceOperationDefinition definition,
    DeviceOperationDiagnosticProvider provider) {
  definition.projection.diagnostic_provider = provider;
  return definition;
}

// Opt in only for implementations with internal synchronization. The default
// remains owner dispatch; no new task or asynchronous request lifetime exists.
constexpr DeviceOperationDefinition device_operation_on_caller(
    DeviceOperationDefinition definition) {
  definition.model.execution = DeviceOperationExecution::Caller;
  definition.model.owner_executor = nullptr;
  return definition;
}

constexpr DeviceOperationDefinition device_operation_cancellable(
    DeviceOperationDefinition definition) {
  definition.model.cancellable = true;
  return definition;
}

constexpr DeviceOperationDefinition device_operation_remote(
    DeviceOperationDefinition definition, DeviceRemoteAccess access) {
  definition.model.remote_access = access;
  return definition;
}

constexpr DeviceOperationDefinition device_operation_diagnostic_access(
    DeviceOperationDefinition definition, DeviceDiagnosticAccess access) {
  definition.model.diagnostic_access = access;
  return definition;
}

constexpr DeviceOperationDefinition device_operation_schemas(
    DeviceOperationDefinition definition, const char *request_schema,
    const char *response_schema) {
  definition.model.request_schema = request_schema;
  definition.model.response_schema = response_schema;
  return definition;
}


inline constexpr size_t kDeviceQueryBodyBytes = 128;
inline constexpr size_t kDeviceControlBodyBytes = 4096;
inline constexpr size_t kDeviceConfigBodyBytes = 8192;
inline constexpr size_t kDeviceDocumentBodyBytes = 65536;

struct DeviceOperationOptions {
  DeviceOperationMethod method = DeviceOperationMethod::Post;
  bool method_explicit = false;
  size_t maximum_request_bytes = kDeviceControlBodyBytes;
  const char *owner_executor = lunaredu::lunaros::kMainServiceExecutor;
};

constexpr DeviceOperationOptions device_operation_default_options() { return {}; }

constexpr DeviceOperationOptions device_operation_method(DeviceOperationMethod method) {
  return {method, true, kDeviceControlBodyBytes, lunaredu::lunaros::kMainServiceExecutor};
}

constexpr DeviceOperationOptions device_operation_body_limit(
    size_t bytes, DeviceOperationOptions options = device_operation_default_options()) {
  options.maximum_request_bytes = bytes;
  return options;
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
  definition.projection.method = DeviceOperationMethod::Get;
  definition.projection.handler_kind = DeviceOperationHandlerKind::JsonQuery;
  definition.projection.json_provider = provider;
  definition.projection.maximum_request_bytes = kDeviceQueryBodyBytes;
  return definition;
}

constexpr DeviceOperationDefinition make_device_json_query(
    const char *operation_id, DeviceJsonResultProvider provider,
    const char *owner_executor = lunaredu::lunaros::kMainServiceExecutor) {
  DeviceOperationDefinition definition;
  definition.model.id = operation_id;
  definition.model.kind = lunaredu::lunaros::DeviceOperationKind::Query;
  definition.model.owner_executor = owner_executor;
  definition.projection.method = DeviceOperationMethod::Get;
  definition.projection.handler_kind = DeviceOperationHandlerKind::JsonQuery;
  definition.projection.json_result_provider = provider;
  definition.projection.maximum_request_bytes = kDeviceQueryBodyBytes;
  return definition;
}

constexpr DeviceOperationDefinition make_device_json_command(
    const char *operation_id, DeviceJsonCommandHandler handler,
    DeviceOperationOptions options = device_operation_default_options()) {
  DeviceOperationDefinition definition;
  definition.model.id = operation_id;
  definition.model.kind = lunaredu::lunaros::DeviceOperationKind::Command;
  definition.projection.method =
      options.method_explicit ? options.method : DeviceOperationMethod::Post;
  definition.model.mutating =
      definition.projection.method != DeviceOperationMethod::Get;
  definition.model.owner_executor = options.owner_executor;
  definition.projection.handler_kind =
      DeviceOperationHandlerKind::JsonCommand;
  definition.projection.json_command_handler = handler;
  definition.projection.maximum_request_bytes = options.maximum_request_bytes;
  return definition;
}

constexpr DeviceOperationDefinition make_device_json_binding(
    const char *id, const DeviceJsonBinding &binding, bool mutating,
    size_t maximum_request_bytes = 128) {
  DeviceOperationDefinition definition;
  definition.model.id = id;
  definition.model.kind = mutating ? DeviceOperationKind::Command : DeviceOperationKind::Query;
  definition.model.mutating = mutating;
  definition.model.owner_executor = kMainServiceExecutor;
  definition.projection.method = mutating ? DeviceOperationMethod::Post : DeviceOperationMethod::Get;
  definition.projection.handler_kind = mutating ? DeviceOperationHandlerKind::JsonCommand : DeviceOperationHandlerKind::JsonQuery;
  definition.projection.json_binding = &binding;
  definition.projection.maximum_request_bytes = maximum_request_bytes;
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
  definition.projection.method = DeviceOperationMethod::Get;
  definition.projection.handler_kind = DeviceOperationHandlerKind::Bulk;
  definition.projection.bulk_handler = handler;
  definition.projection.maximum_request_bytes = kDeviceQueryBodyBytes;
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
  definition.projection.method =
      options.method_explicit ? options.method : DeviceOperationMethod::Put;
  definition.projection.handler_kind = DeviceOperationHandlerKind::Bulk;
  definition.projection.bulk_handler = handler;
  definition.projection.maximum_request_bytes = options.maximum_request_bytes;
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
  // Stream handlers enforce their own total size/partition/quota limits.
  definition.projection.maximum_request_bytes = 0;
  return definition;
}

constexpr DeviceOperationDefinition make_device_media(
    const char *operation_id, DeviceMediaHandler handler,
    const char *owner_executor = lunaredu::lunaros::kMainServiceExecutor) {
  DeviceOperationDefinition definition;
  definition.model.id = operation_id;
  definition.model.kind = lunaredu::lunaros::DeviceOperationKind::Stream;
  definition.model.owner_executor = owner_executor;
  definition.projection.method = DeviceOperationMethod::Get;
  definition.projection.handler_kind = DeviceOperationHandlerKind::Media;
  definition.projection.media_handler = handler;
  return definition;
}

} // namespace lunaredu::lunaros
