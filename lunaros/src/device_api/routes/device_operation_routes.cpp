#include "device_api/routes/device_operation_routes.h"
#include "device_api/routes/device_operation_dispatch.h"

#include <cstdlib>
#include <string>

#include <ArduinoJson.h>

#include "lunaredu_lunaros/device_api/device_api.h"
#include "lunaredu_lunaros/device_api/device_projection.h"
#include "device_api/access/device_api_request_guard.h"
#include "device_api/http/embedded_http_server.h"
#include "device_api/http/http_request_api.h"
#include "device_api/routes/device_api_route_support.h"

using namespace lunaredu::lunaros;

namespace {

struct MediaWriterContext {
  bool response_started = false;
  void *transport_context = nullptr;
};

struct JsonOperationInvocation {
  const DeviceOperationProjection *descriptor = nullptr;
  JsonVariantConst request;
  JsonObject response;
  DeviceOperationResult result;
};

struct BulkOperationInvocation {
  const DeviceOperationProjection *descriptor = nullptr;
  DeviceBulkRequest request;
  JsonObject response;
  DeviceOperationResult result;
};

struct MediaOperationInvocation {
  const DeviceOperationProjection *descriptor = nullptr;
  DeviceMediaRequest request;
  DeviceMediaWriter writer;
  DeviceOperationResult result;
};

struct ResolvedDeviceOperation {
  const lunaredu::lunaros::DeviceModuleSpec *module = nullptr;
  const lunaredu::lunaros::DeviceOperationSpec *model = nullptr;
  const DeviceOperationProjection *projection = nullptr;
};

std::string semantic_name(const std::string &module,
                          const std::string &operation) {
  return module + "." + operation;
}

bool resolve_allowed_operation(std::string &module, std::string &operation,
                               ResolvedDeviceOperation &resolved) {
  resolved = {};
  if (!parse_device_module_path(module, operation)) {
    send_device_api_error(404, "operation_not_found");
    return false;
  }
  resolved.module =
      lunaredu::lunaros::find_device_module_spec(module.c_str());
  resolved.model = lunaredu::lunaros::find_device_operation_spec(
      module.c_str(), operation.c_str());
  resolved.projection =
      find_device_operation_projection(module.c_str(), operation.c_str());
  if (resolved.module == nullptr || resolved.model == nullptr ||
      resolved.projection == nullptr) {
    send_device_api_error(404, "operation_not_found");
    return false;
  }
  const std::string semantic = semantic_name(module, operation);
  if (!device_api_current_request_allowed(DeviceApiAccessSurface::Operation,
                                          semantic.c_str())) {
    send_device_api_error(403, "provisioning_surface_restricted");
    return false;
  }
  if (resolved.projection->method != current_device_operation_method()) {
    send_device_api_error(405, "method_not_allowed");
    return false;
  }
  if (!device_capability_active(resolved.module->capability_id)) {
    send_device_api_error(503, "capability_unavailable");
    return false;
  }
  return true;
}

bool request_has_arg_adapter(const char *name, void *context) {
  return request_context_has_arg(name, context);
}

std::string request_arg_adapter(const char *name, void *context) {
  return request_context_arg(name, context);
}

int read_chunk_adapter(uint8_t *buffer, size_t size, void *context) {
  return read_request_body_chunk_from_context(buffer, size, context);
}

bool media_send_binary(int status_code, const char *content_type,
                       const uint8_t *data, size_t size, void *context) {
  auto *writer_context = static_cast<MediaWriterContext *>(context);
  const bool sent = writer_context != nullptr &&
                    send_http_binary_response_from_context(
                        status_code, content_type, data, size,
                        writer_context->transport_context);
  if (sent && writer_context != nullptr) {
    writer_context->response_started = true;
  }
  return sent;
}

bool media_begin_stream(int status_code, const char *content_type,
                        void *context) {
  auto *writer_context = static_cast<MediaWriterContext *>(context);
  const bool started = writer_context != nullptr &&
                       begin_http_stream_from_context(
                           status_code, content_type,
                           writer_context->transport_context);
  if (started && writer_context != nullptr) {
    writer_context->response_started = true;
  }
  return started;
}

bool media_send_chunk(const uint8_t *data, size_t size, void *context) {
  auto *writer_context = static_cast<MediaWriterContext *>(context);
  return writer_context != nullptr &&
         send_http_chunk_from_context(data, size,
                                      writer_context->transport_context);
}

void media_end_stream(void *context) {
  auto *writer_context = static_cast<MediaWriterContext *>(context);
  if (writer_context != nullptr) {
    end_http_stream_from_context(writer_context->transport_context);
  }
}

void execute_json_binding(void *opaque,
                          lunaredu::lunaros::ServiceOperationResult &status) {
  auto &invocation = *static_cast<JsonOperationInvocation *>(opaque);
  const DeviceOperationProjection &descriptor = *invocation.descriptor;
  if (descriptor.handler_kind == DeviceOperationHandlerKind::JsonQuery) {
    if (descriptor.json_result_provider != nullptr) {
      invocation.result =
          descriptor.json_result_provider(invocation.response);
    } else if (descriptor.json_provider != nullptr) {
      descriptor.json_provider(invocation.response);
    } else {
      invocation.result = {false, 501, "json_provider_unavailable"};
    }
  } else if (descriptor.handler_kind ==
             DeviceOperationHandlerKind::JsonCommand) {
    invocation.result =
        descriptor.json_command_handler != nullptr
            ? descriptor.json_command_handler(invocation.request,
                                               invocation.response)
            : DeviceOperationResult{false, 501,
                                    "json_command_handler_unavailable"};
  } else {
    invocation.result = {false, 500, "invalid_json_dispatch"};
  }
  status.ok = true;
}

void execute_bulk_binding(void *opaque,
                          lunaredu::lunaros::ServiceOperationResult &status) {
  auto &invocation = *static_cast<BulkOperationInvocation *>(opaque);
  invocation.result = invocation.descriptor->bulk_handler != nullptr
                          ? invocation.descriptor->bulk_handler(
                                invocation.request, invocation.response)
                          : DeviceOperationResult{
                                false, 501, "bulk_handler_unavailable"};
  status.ok = true;
}

void execute_media_binding(void *opaque,
                           lunaredu::lunaros::ServiceOperationResult &status) {
  auto &invocation = *static_cast<MediaOperationInvocation *>(opaque);
  invocation.result = invocation.descriptor->media_handler != nullptr
                          ? invocation.descriptor->media_handler(
                                invocation.request, invocation.writer)
                          : DeviceOperationResult{
                                false, 501, "media_handler_unavailable"};
  status.ok = true;
}

bool dispatch_to_owner(
                       const lunaredu::lunaros::DeviceOperationSpec &model,
                       lunaredu::lunaros::ServiceOperation operation,
                       void *context, DeviceOperationResult &result) {
  std::string error;
  if (!lunaredu::lunaros::internal::dispatch_device_operation(
          model, operation, context, error)) {
    result = {false, 503,
              error.empty() ? "operation_owner_unavailable" : error};
    return false;
  }
  return true;
}

void handle_media(
    const lunaredu::lunaros::DeviceOperationSpec &model,
    const DeviceOperationProjection &projection) {
  add_response_header("Cache-Control", "no-store, no-cache, must-revalidate");
  add_response_header("Pragma", "no-cache");
  DeviceMediaRequest request;
  request.path = request_arg("path");
  request.fps = request_has_arg("fps")
                    ? std::atoi(request_arg("fps").c_str())
                    : 0;
  MediaWriterContext context;
  context.transport_context = current_request_context_handle();
  const DeviceMediaWriter writer = {
      .context = &context,
      .send_binary = media_send_binary,
      .begin_stream = media_begin_stream,
      .send_chunk = media_send_chunk,
      .end_stream = media_end_stream,
  };
  MediaOperationInvocation invocation{&projection, request, writer, {}};
  (void)dispatch_to_owner(model, execute_media_binding, &invocation,
                          invocation.result);
  const DeviceOperationResult &result = invocation.result;
  if (!result.ok && !context.response_started) {
    send_device_api_error(result.status_code == 0 ? 500 : result.status_code,
                          result.error.empty() ? "media_request_failed"
                                               : result.error.c_str());
  }
}

void handle_module_operation() {
  std::string module;
  std::string operation;
  ResolvedDeviceOperation resolved;
  if (!resolve_allowed_operation(module, operation, resolved)) {
    return;
  }
  const DeviceOperationProjection &projection = *resolved.projection;
  if (projection.handler_kind == DeviceOperationHandlerKind::Media) {
    if (projection.media_handler == nullptr) {
      send_device_api_error(501, "media_handler_unavailable");
      return;
    }
    handle_media(*resolved.model, projection);
    return;
  }

  JsonDocument input;
  if (request_body_size() > 0 && !parse_json_body(input)) {
    send_device_api_error(400, "invalid_json");
    return;
  }
  JsonVariantConst payload = input.as<JsonVariantConst>();
  JsonDocument response;
  JsonObject data = response["data"].to<JsonObject>();
  DeviceOperationResult result;

  switch (projection.handler_kind) {
  case DeviceOperationHandlerKind::JsonQuery:
  case DeviceOperationHandlerKind::JsonCommand: {
    JsonOperationInvocation invocation{&projection, payload, data, {}};
    (void)dispatch_to_owner(*resolved.model, execute_json_binding, &invocation,
                            invocation.result);
    result = invocation.result;
    break;
  }
  case DeviceOperationHandlerKind::Bulk:
  case DeviceOperationHandlerKind::Upload: {
    DeviceBulkRequest request;
    request.method = current_device_operation_method();
    request.payload = payload;
    request.body_data = request_body_data();
    request.body_size = request_body_size();
    request.content_length = request_content_length();
    request.context = current_request_context_handle();
    request.has_arg = request_has_arg_adapter;
    request.arg = request_arg_adapter;
    request.read_chunk = read_chunk_adapter;
    BulkOperationInvocation invocation{&projection, request, data, {}};
    (void)dispatch_to_owner(*resolved.model, execute_bulk_binding, &invocation,
                            invocation.result);
    result = invocation.result;
    break;
  }
  case DeviceOperationHandlerKind::Media:
    result = {false, 500, "invalid_media_dispatch"};
    break;
  }
  send_device_operation_result(response, result);
}

bool should_buffer_module_body() {
  std::string module;
  std::string operation;
  if (!parse_device_module_path(module, operation)) {
    return true;
  }
  const DeviceOperationProjection *descriptor =
      find_device_operation_projection(module.c_str(), operation.c_str());
  return descriptor == nullptr ||
         descriptor->handler_kind != DeviceOperationHandlerKind::Upload;
}

} // namespace

void register_device_operation_routes() {
  register_http_route_with_body_policy("/api/v1/modules/*", HTTP_GET,
                                       handle_module_operation,
                                       should_buffer_module_body);
  register_http_route_with_body_policy("/api/v1/modules/*", HTTP_POST,
                                       handle_module_operation,
                                       should_buffer_module_body);
  register_http_route_with_body_policy("/api/v1/modules/*", HTTP_PUT,
                                       handle_module_operation,
                                       should_buffer_module_body);
  register_http_route_with_body_policy("/api/v1/modules/*", HTTP_DELETE,
                                       handle_module_operation,
                                       should_buffer_module_body);
}
