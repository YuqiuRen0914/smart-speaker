#include "device_api/device_operation_builders.h"
#include "lunaros_python_application.h"
#include "lunaros_micropython_runtime_config.h"
#include "model/device_model.h"
#include "lunaros_python_device_api.h"

#include "device_handlers.h"

#include "device_api/device_projection.h"
#include <esp_log.h>

using namespace lunaredu::lunaros;

namespace {

DeviceProjectionBinding g_projection;

const DeviceCapabilityDescriptor kMicroPythonCapability = []() {
  DeviceCapabilityDescriptor descriptor;
  descriptor.id = "micropython_runtime";
  return descriptor;
}();

constexpr const char *kMicroPythonModuleId = "micropython";

constexpr const char *kProjectUploadRequestSchema = R"json({"type":"object","additionalProperties":false,"required":["manifest","entry_name","content"],"properties":{"manifest":{"type":"object"},"entry_name":{"type":"string","minLength":1,"maxLength":64},"content":{"type":"string","minLength":1},"autorun":{"type":"boolean"},"run":{"type":"boolean"}}})json";
constexpr const char *kProjectUploadResponseSchema = R"json({"type":"object","required":["ok","state","activity_id","project_id","version","entry_name","rollback_available"],"properties":{"ok":{"type":"boolean"},"state":{"const":"succeeded"},"activity_id":{"type":"string"},"project_id":{"type":"string"},"version":{"type":"string"},"entry_name":{"type":"string"},"rollback_available":{"type":"boolean"}}})json";
constexpr const char *kProjectIdRequestSchema = R"json({"type":"object","additionalProperties":false,"required":["project_id"],"properties":{"project_id":{"type":"string","minLength":1,"maxLength":96},"run":{"type":"boolean"}}})json";
constexpr const char *kProjectActionRequestSchema = R"json({"type":"object","additionalProperties":false,"required":["project_id","action_id"],"properties":{"project_id":{"type":"string","minLength":1,"maxLength":96},"action_id":{"type":"string","minLength":1,"maxLength":96}}})json";
constexpr const char *kProjectLifecycleResponseSchema = R"json({"type":"object","required":["ok","state","activity_id","project_id"],"properties":{"ok":{"type":"boolean"},"state":{"const":"succeeded"},"activity_id":{"type":"string"},"project_id":{"type":"string"}}})json";
constexpr const char *kProjectListResponseSchema = R"json({"type":"object","required":["ok","count","projects"],"properties":{"ok":{"type":"boolean"},"count":{"type":"integer","minimum":0},"projects":{"type":"array"}}})json";
constexpr const char *kProjectPreviewRequestSchema = R"json({"type":"object","additionalProperties":false,"required":["manifest","entry_name","content"],"properties":{"manifest":{"type":"object"},"entry_name":{"type":"string","minLength":1,"maxLength":64},"content":{"type":"string","minLength":1}}})json";

constexpr DeviceOperationDefinition make_device_json_read_command(
    const char *operation_id, DeviceJsonCommandHandler handler) {
  DeviceOperationDefinition definition =
      make_device_json_command(operation_id, handler);
  definition.model.kind = DeviceOperationKind::Query;
  definition.model.mutating = false;
  return definition;
}

constexpr DeviceOperationDefinition kMicroPythonOperations[] = {
    make_device_json_provider("capabilities.get", fill_micropython_capabilities_payload),
    make_device_json_query("runtime.status.get", fill_micropython_runtime_status_payload),
    make_device_json_command("runtime.enter", handle_micropython_runtime_enter),
    make_device_json_command("runtime.leave", handle_micropython_runtime_leave),
    make_device_json_command("repl.enter", handle_micropython_repl_enter),
    make_device_json_command("repl.leave", handle_micropython_repl_leave),
    make_device_json_command("repl.feed", handle_micropython_repl_feed),
    make_device_json_command("runtime.logs.get", fill_micropython_runtime_logs_payload, device_operation_method(DeviceOperationMethod::Get)),
    make_device_json_query("script.list", fill_micropython_script_list_payload),
    make_device_json_command("script.run", handle_micropython_script_run),
    make_device_json_command("script.stop", handle_micropython_script_stop),
    make_device_json_command("script.delete", handle_micropython_script_delete),
    make_device_json_command("script.autorun.set", handle_micropython_script_autorun_set),
    make_device_bulk_write("script.upload", handle_micropython_script_upload_bulk_payload),
    device_operation_schemas(
        make_device_json_query("project.list", fill_micropython_project_list_payload),
        R"json({"type":"object","additionalProperties":false})json",
        kProjectListResponseSchema),
    device_operation_schemas(
        make_device_bulk_write("project.upload", handle_micropython_project_upload_bulk_payload),
        kProjectUploadRequestSchema, kProjectUploadResponseSchema),
    device_operation_schemas(
        make_device_json_command("project.preview", handle_micropython_project_preview,
                                 device_operation_body_limit(kDeviceDocumentBodyBytes)),
        kProjectPreviewRequestSchema, R"json({"type":"object"})json"),
    device_operation_schemas(
        make_device_json_command("project.run", handle_micropython_project_run),
        kProjectIdRequestSchema, kProjectLifecycleResponseSchema),
    device_operation_schemas(
        make_device_json_read_command("project.surface.get", handle_micropython_project_surface_get),
        kProjectIdRequestSchema, R"json({"type":"object","required":["ok","project_id","version","surface","bindings"]})json"),
    device_operation_schemas(
        make_device_json_command("project.action.invoke", handle_micropython_project_action_invoke),
        kProjectActionRequestSchema, kProjectLifecycleResponseSchema),
    device_operation_schemas(
        make_device_json_command("project.rollback", handle_micropython_project_rollback),
        kProjectIdRequestSchema, kProjectLifecycleResponseSchema),
    device_operation_schemas(
        make_device_json_command("project.delete", handle_micropython_project_delete),
        kProjectIdRequestSchema, kProjectLifecycleResponseSchema),
};

const DeviceModuleProjectionMetadata kMicroPythonLocalApiMetadata = {
    kMicroPythonModuleId,
    "micropython",
};

const DeviceModuleProjection kMicroPythonLocalApi = {
    "micropython_runtime",
    &kMicroPythonLocalApiMetadata,
    kMicroPythonOperations,
    sizeof(kMicroPythonOperations) / sizeof(kMicroPythonOperations[0]),
};

} // namespace

namespace lunaredu::python {
namespace {
bool set_projection_available(bool, std::string &);
}
bool install_projection(DeviceProjectionRegistry &registry,
                                    std::string &error) {
  micropython::configure_runtime_control_surface({set_projection_available,
    [](const lunaros::DeviceApiContext &device) -> const char * {
      return SystemApplication::foreground(device) ? kApplicationId : nullptr;
    }});
  return g_projection.declare(
      registry, {&kMicroPythonCapability, &kMicroPythonLocalApi, nullptr, false},
      error);
}

namespace {
bool set_projection_available(bool available,
                                          std::string &error) {
  error.clear();
  if (!available) {
    g_projection.deactivate(kMicroPythonCapability.id);
    return true;
  }
  return g_projection.activate(kMicroPythonCapability, &error);
}

} // namespace
lunaros::ProductDeclarationNode projection_declaration_node(lunaros::DeviceProjectionRegistry &registry) {
  return {"micropython", [](void *context, std::string &error) {
    return !micropython::core_enabled() || install_projection(*static_cast<lunaros::DeviceProjectionRegistry *>(context), error);
  }, &registry};
}
} // namespace lunaredu::python
