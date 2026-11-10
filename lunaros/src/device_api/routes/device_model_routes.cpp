#include "device_api/routes/device_model_routes.h"

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

struct ModuleProjectionLookup {
  const char *id = nullptr;
  const DeviceModuleProjection *descriptor = nullptr;
};

void find_module_projection(const DeviceModuleProjection &descriptor,
                            void *opaque) {
  auto &lookup = *static_cast<ModuleProjectionLookup *>(opaque);
  if (lookup.descriptor == nullptr && descriptor.module != nullptr &&
      descriptor.module->id != nullptr && lookup.id != nullptr &&
      std::string(descriptor.module->id) == lookup.id) {
    lookup.descriptor = &descriptor;
  }
}

void append_module(const lunaredu::lunaros::DeviceModuleSpec &spec,
                   void *opaque) {
  if (spec.id == nullptr) {
    return;
  }
  JsonArray modules = *static_cast<JsonArray *>(opaque);
  JsonObject module = modules.add<JsonObject>();
  module["id"] = spec.id;
  module["kind"] = spec.kind != nullptr ? spec.kind : spec.id;
  module["capability"] = spec.capability_id;
  ModuleProjectionLookup projection{spec.id};
  visit_device_module_projections(find_module_projection, &projection);
  if (projection.descriptor != nullptr &&
      projection.descriptor->module->fill_contract != nullptr) {
    projection.descriptor->module->fill_contract(
        module["contract"].to<JsonObject>());
  }
  JsonArray operations = module["operations"].to<JsonArray>();
  for (size_t index = 0; index < spec.operation_count; ++index) {
    const auto &operation = spec.operations[index];
    if (operation.id == nullptr) {
      continue;
    }
    const DeviceOperationProjection *binding =
        find_device_operation_projection(spec.id, operation.id);
    if (binding == nullptr) {
      continue;
    }
    JsonObject item = operations.add<JsonObject>();
    item["id"] = operation.id;
    item["method"] = device_operation_method_name(binding->method);
    item["path"] = std::string(kDeviceModuleRoutePrefix) +
                   spec.id + "/" + operation.id;
    item["kind"] = lunaredu::lunaros::device_operation_kind_name(
        operation.kind);
    item["mutating"] = operation.mutating;
    item["handler"] =
        device_operation_handler_kind_name(binding->handler_kind);
    item["response"] =
        operation.kind == lunaredu::lunaros::DeviceOperationKind::Stream
            ? "stream"
            : "json";
  }
}

void handle_device() {
  if (!device_api_current_request_allowed(DeviceApiAccessSurface::Metadata)) {
    send_device_api_error(403, "provisioning_surface_restricted");
    return;
  }
  JsonDocument response;
  response["apiVersion"] = kDeviceApiVersion;
  response["ok"] = true;
  JsonObject data = response["data"].to<JsonObject>();
  fill_device_info_payload(data["device"].to<JsonObject>());
  fill_device_status_sections(data["status"].to<JsonObject>());
  serialize_and_send_json(200, response);
}

void handle_capabilities() {
  if (!device_api_current_request_allowed(DeviceApiAccessSurface::Metadata)) {
    send_device_api_error(403, "provisioning_surface_restricted");
    return;
  }
  JsonDocument response;
  response["apiVersion"] = kDeviceApiVersion;
  response["ok"] = true;
  JsonArray modules = response["data"]["modules"].to<JsonArray>();
  lunaredu::lunaros::visit_device_model_modules(append_module, &modules);
  serialize_and_send_json(200, response);
}

} // namespace

void register_device_model_routes() {
  register_http_route("/api/v1/device", HTTP_GET, handle_device);
  register_http_route("/api/v1/capabilities", HTTP_GET, handle_capabilities);
}
