#include "kernel/runtime.h"
#include "device_api/device_api_context.h"
#include "device_api/routes/device_api_route_support.h"
#include "device_api/routes/device_api_routes.h"
#include "device_api/operation_dispatch.h"

#include <cstdlib>
#include <string>

#include <ArduinoJson.h>

#include "device_api/device_api.h"
#include "device_api/device_projection.h"
#include "device_api/operation_feedback.h"
#include "device_api/access/device_api_request_guard.h"
#include "device_api/access/device_api_bound_access.h"
#include "diagnostics/session.h"
#include "device_api/http/embedded_http_server.h"
#include "device_api/http/http_request_api.h"

using namespace lunaredu::lunaros;

namespace {

struct MediaWriterContext {
  bool response_started = false;
  void *transport_context = nullptr;
};

struct JsonOperationInvocation {
  DeviceApiContext *device = nullptr;
  const DeviceOperationProjection *descriptor = nullptr;
  JsonVariantConst request;
  JsonObject response;
  DeviceOperationResult result;
  std::string shared_grant;
};

struct BulkOperationInvocation {
  DeviceApiContext *device = nullptr;
  const DeviceOperationProjection *descriptor = nullptr;
  DeviceBulkRequest request;
  JsonObject response;
  DeviceOperationResult result;
};

struct MediaOperationInvocation {
  DeviceApiContext *device = nullptr;
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

bool resolve_allowed_operation(DeviceProjectionRegistry &registry, std::string &module, std::string &operation,
                               ResolvedDeviceOperation &resolved) {
  resolved = {};
  if (!parse_device_module_path(module, operation)) {
    send_device_api_error(404, "operation_not_found");
    return false;
  }
  resolved.module =
      registry.context().models.find_module(module.c_str());
  resolved.model = registry.context().models.find_operation(
      module.c_str(), operation.c_str());
  resolved.projection =
      registry.find_operation(module.c_str(), operation.c_str());
  if (resolved.module == nullptr || resolved.model == nullptr ||
      resolved.projection == nullptr) {
    send_device_api_error(404, "operation_not_found");
    return false;
  }
  const auto grant = request_header("X-Lunar-Access-Grant");
  if (device_api_bound_access_required() && grant.rfind("lls1|", 0) == 0) {
    if (!device_api_shared_access_authorized(grant.c_str())) {
      send_device_api_error(401, "shared_grant_expired");
      return false;
    }
    if (!device_api_shared_operation_allowed(module.c_str(), operation.c_str())) {
      send_device_api_error(403, "shared_operation_forbidden");
      return false;
    }
  }
  const std::string semantic = semantic_name(module, operation);
  const std::string debug_token = request_header("X-Lunar-Debug-Token");
  if (!device_api_current_request_allowed(DeviceApiAccessSurface::Operation,
                                          semantic.c_str(), debug_token.c_str())) {
    send_device_api_error(403, "provisioning_surface_restricted");
    return false;
  }
  if (!device_api_diagnostic_access_authorized(
          resolved.model, debug_token.c_str())) {
    send_device_api_error(401, "debug_session_required");
    return false;
  }
  if (resolved.projection->method != current_device_operation_method()) {
    send_device_api_error(405, "method_not_allowed");
    return false;
  }
  if (!registry.context().capabilities.active(resolved.module->capability_id)) {
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
  // Queued work must recheck its captured grant on the owning runtime thread.
  if (!invocation.shared_grant.empty() &&
      !device_api_shared_access_authorized(invocation.shared_grant.c_str())) {
    invocation.result = {false, 401, "shared_grant_expired"};
  } else if (descriptor.json_binding != nullptr) {
    invocation.result = descriptor.json_binding->invoke(
        descriptor.json_binding->context, invocation.request, invocation.response);
  } else if (descriptor.handler_kind == DeviceOperationHandlerKind::JsonQuery) {
    if (descriptor.json_result_provider != nullptr) {
      invocation.result =
          descriptor.json_result_provider(*invocation.device, invocation.response);
    } else if (descriptor.json_provider != nullptr) {
      descriptor.json_provider(*invocation.device, invocation.response);
    } else {
      invocation.result = {false, 501, "json_provider_unavailable"};
    }
  } else if (descriptor.handler_kind ==
             DeviceOperationHandlerKind::JsonCommand) {
    invocation.result =
        descriptor.json_command_handler != nullptr
            ? descriptor.json_command_handler(*invocation.device, invocation.request,
                                               invocation.response)
            : DeviceOperationResult{false, 501,
                                    "json_command_handler_unavailable"};
  } else {
    invocation.result = {false, 500, "invalid_json_dispatch"};
  }
  status.work_result_known = true;
  status.work_ok = invocation.result.ok;
  status.work_error_code = invocation.result.error.c_str();
  status.ok = true;
}

void execute_bulk_binding(void *opaque,
                          lunaredu::lunaros::ServiceOperationResult &status) {
  auto &invocation = *static_cast<BulkOperationInvocation *>(opaque);
  invocation.result = invocation.descriptor->bulk_handler != nullptr
                          ? invocation.descriptor->bulk_handler(
                                *invocation.device, invocation.request, invocation.response)
                          : DeviceOperationResult{
                                false, 501, "bulk_handler_unavailable"};
  status.work_result_known = true;
  status.work_ok = invocation.result.ok;
  status.work_error_code = invocation.result.error.c_str();
  status.ok = true;
}

void execute_media_binding(void *opaque,
                           lunaredu::lunaros::ServiceOperationResult &status) {
  auto &invocation = *static_cast<MediaOperationInvocation *>(opaque);
  invocation.result = invocation.descriptor->media_handler != nullptr
                          ? invocation.descriptor->media_handler(
                                *invocation.device, invocation.request, invocation.writer)
                          : DeviceOperationResult{
                                false, 501, "media_handler_unavailable"};
  status.work_result_known = true;
  status.work_ok = invocation.result.ok;
  status.work_error_code = invocation.result.error.c_str();
  status.ok = true;
}

bool dispatch_to_owner(
                       lunaredu::lunaros::KernelRuntime &runtime,
                       const lunaredu::lunaros::DeviceOperationSpec &model,
                       lunaredu::lunaros::ServiceOperation operation,
                       void *context, DeviceOperationResult &result, const char *module,
                       DeviceOperationCorrelation *correlation = nullptr) {
  std::string error;
  if (!lunaredu::lunaros::dispatch_device_operation(
          runtime, model, operation, context, error, module, correlation)) {
    result = {false, 503,
              error.empty() ? "operation_owner_unavailable" : error};
    return false;
  }
  return true;
}

void handle_media(DeviceApiContext &device,
    const lunaredu::lunaros::DeviceOperationSpec &model,
    const DeviceOperationProjection &projection, const char *module) {
  add_response_header("Cache-Control", "no-store, no-cache, must-revalidate");
  add_response_header("Pragma", "no-cache");
  DeviceMediaRequest request;
  request.path = request_arg("path");
  request.fps = request_has_arg("fps")
                    ? std::atoi(request_arg("fps").c_str())
                    : 0;
  request.frames = request_has_arg("frames")
                       ? std::atoi(request_arg("frames").c_str())
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
  MediaOperationInvocation invocation{&device, &projection, request, writer, {}};
  (void)dispatch_to_owner(device.runtime, model, execute_media_binding, &invocation,
                          invocation.result, module);
  const DeviceOperationResult &result = invocation.result;
  if (!result.ok && !context.response_started) {
    send_device_api_error(result.status_code == 0 ? 500 : result.status_code,
                          result.error.empty() ? "media_request_failed"
                                               : result.error.c_str());
  }
}

void handle_module_operation(DeviceProjectionRegistry &registry) {
  std::string module;
  std::string operation;
  ResolvedDeviceOperation resolved;
  if (!resolve_allowed_operation(registry, module, operation, resolved)) {
    return;
  }
  const DeviceOperationProjection &projection = *resolved.projection;
  if (projection.maximum_request_bytes &&
      request_content_length() > projection.maximum_request_bytes) {
    send_device_api_error(413, "request_body_too_large");
    return;
  }
  if (projection.handler_kind == DeviceOperationHandlerKind::Media) {
    if (projection.media_handler == nullptr) {
      send_device_api_error(501, "media_handler_unavailable");
      return;
    }
    handle_media(registry.device(), *resolved.model, projection, module.c_str());
    return;
  }

  auto input=make_http_json_document();
  if (request_body_size() > 0 && !parse_json_body(input)) {
    send_device_api_error(400, "invalid_json");
    return;
  }
  const auto grant = request_header("X-Lunar-Access-Grant");
  if (operation.rfind("teleop.", 0) == 0 && operation != "teleop.state.get" &&
      device_api_shared_access_authorized(grant.c_str()) &&
      !device_api_shared_control_id_authorized(grant.c_str(), input["control_id"] | "")) {
    send_device_api_error(403, "shared_controller_mismatch");
    return;
  }
  JsonVariantConst payload = input.as<JsonVariantConst>();
  auto response=make_http_json_document();
  JsonObject data = response["result"].to<JsonObject>();
  DeviceOperationResult result;
  auto correlation = begin_device_operation_feedback(registry.device());

  switch (projection.handler_kind) {
  case DeviceOperationHandlerKind::JsonQuery:
  case DeviceOperationHandlerKind::JsonCommand: {
      JsonOperationInvocation invocation{&registry.device(), &projection, payload, data, {},
          device_api_bound_access_required() && grant.rfind("lls1|", 0) == 0 ? grant : ""};
    (void)dispatch_to_owner(registry.runtime(), *resolved.model, execute_json_binding, &invocation,
                            invocation.result, module.c_str(), &correlation);
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
    request.header = request_context_header;
    request.read_chunk = read_chunk_adapter;
    request.administrator_authorized = device_api_admin_access_authorized(
        request_header("X-Lunar-Debug-Token").c_str());
    BulkOperationInvocation invocation{&registry.device(), &projection, request, data, {}};
    (void)dispatch_to_owner(registry.runtime(), *resolved.model, execute_bulk_binding, &invocation,
                            invocation.result, module.c_str(), &correlation);
    result = invocation.result;
    break;
  }
  case DeviceOperationHandlerKind::Media:
    result = {false, 500, "invalid_media_dispatch"};
    break;
  }
  finish_device_operation_feedback(registry.device(), *resolved.module, *resolved.model,
      result, data, response.as<JsonObject>(), correlation);
  send_device_operation_result(response, result);
}

bool should_buffer_module_body(DeviceProjectionRegistry &registry) {
  std::string module;
  std::string operation;
  if (!parse_device_module_path(module, operation)) {
    return true;
  }
  const DeviceOperationProjection *descriptor =
      registry.find_operation(module.c_str(), operation.c_str());
  // Reject in the handler without allocating the oversized buffer first.
  if (descriptor && descriptor->maximum_request_bytes &&
      request_content_length() > descriptor->maximum_request_bytes) return false;
  return descriptor == nullptr ||
         descriptor->handler_kind != DeviceOperationHandlerKind::Upload;
}

} // namespace

void register_device_operation_routes(lunaredu::lunaros::DeviceProjectionRegistry &registry) {
  register_http_route_with_body_policy("/api/v1/modules/*", HTTP_GET,
                                       [&registry] { handle_module_operation(registry); },
                                       [&registry] { return should_buffer_module_body(registry); });
  register_http_route_with_body_policy("/api/v1/modules/*", HTTP_POST,
                                       [&registry] { handle_module_operation(registry); },
                                       [&registry] { return should_buffer_module_body(registry); });
  register_http_route_with_body_policy("/api/v1/modules/*", HTTP_PUT,
                                       [&registry] { handle_module_operation(registry); },
                                       [&registry] { return should_buffer_module_body(registry); });
  register_http_route_with_body_policy("/api/v1/modules/*", HTTP_DELETE,
                                       [&registry] { handle_module_operation(registry); },
                                       [&registry] { return should_buffer_module_body(registry); });
}
