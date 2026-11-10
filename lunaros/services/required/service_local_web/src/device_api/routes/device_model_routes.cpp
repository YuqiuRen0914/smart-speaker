#include "lunaredu_device_contract.h"
#include "device_api/routes/device_api_route_support.h"
#include "device_api/routes/device_api_routes.h"

#include <string>
#include <string_view>
#include "device_api/http/segmented_json.h"
#include <ctime>

#include <ArduinoJson.h>
#ifdef ESP_PLATFORM
#include <esp_app_desc.h>
#endif

#include "device_api/device_api.h"
#include "device_api/device_projection.h"
#include "device_api/access/device_api_request_guard.h"
#include "device_api/access/device_api_bound_access.h"
#include "lunaros_platform_link.h"
#include "system/identity.h"
#include "platform/system.h"
#include "device_api/http/embedded_http_server.h"
#include "device_api/http/http_request_api.h"

using namespace lunaredu::lunaros;
using lunaredu::lunaros::internal::JsonStreamSink;
using lunaredu::lunaros::internal::write_json_object_prefix;
using lunaredu::lunaros::internal::set_joined_json_string;

namespace {

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

struct ModuleProjectionLookup {
  const char *id = nullptr;
  const DeviceModuleProjection *descriptor = nullptr;
};

struct LunarLinkManifestContext {
  DeviceProjectionRegistry &registry;
  bool has_display = false;
  bool has_audio = false;
  bool has_camera = false;
  bool has_motion = false;
  bool has_network = false;
  bool has_storage = false;
  bool motion_available = false;
  bool has_project_lifecycle = false;
  bool has_project_declarative = false;
};

void find_module_projection(const DeviceModuleProjection &descriptor,
                            void *opaque);

const char *lunarlink_operation_kind(const DeviceOperationSpec &operation) {
  switch (operation.kind) {
  case DeviceOperationKind::Query:
  case DeviceOperationKind::ResourceRead:
    return "query";
  case DeviceOperationKind::Stream:
    return "stream";
  case DeviceOperationKind::ResourceWrite:
  case DeviceOperationKind::Upload:
    return "activity";
  case DeviceOperationKind::Command:
    return operation.mutating ? "activity" : "command";
  }
  return "command";
}

const char *lunarlink_risk(std::string_view module_id,
                           const DeviceOperationSpec &operation) {
  if (!operation.mutating && operation.kind != DeviceOperationKind::Stream)
    return "read";
  const std::string_view operation_id = operation.id == nullptr ? "" : operation.id;
  if (module_id.find("camera") != std::string_view::npos ||
      operation_id.find("camera") != std::string_view::npos ||
      module_id == "assistant")
    return "private_input";
  if (module_id == "skateboard" ||
      module_id.find("motion") != std::string_view::npos ||
      module_id.find("motor") != std::string_view::npos)
    return "physical";
  if (module_id.find("network") != std::string_view::npos)
    return "network";
  if (module_id.find("screen") != std::string_view::npos ||
      module_id.find("display") != std::string_view::npos ||
      module_id.find("eyes") != std::string_view::npos)
    return "visible_output";
  if (module_id == "ota" || module_id == "debug" ||
      module_id == "filesystem" || module_id == "applications")
    return "maintenance";
  return "visible_output";
}

bool add_schema(JsonObject destination, const char *encoded) {
  if(destination.isNull())return false;
  if (encoded != nullptr && encoded[0] != '\0') {
    auto parsed=make_http_json_document();
    const auto error=deserializeJson(parsed,encoded);
    if(error==DeserializationError::NoMemory||parsed.overflowed())return false;
    if (error == DeserializationError::Ok && parsed.is<JsonObject>())
      return destination.set(parsed.as<JsonObjectConst>());
  }
  destination["type"] = "object";
  destination["additionalProperties"] = false;
  return !destination["additionalProperties"].isNull();
}

bool fill_documentation(JsonObject documentation, std::string_view subject,
                        const char *operation_id = nullptr) {
  if(!set_joined_json_string(documentation,"summary",{"LunarOS capability ",subject,
      operation_id?".":"",operation_id?operation_id:"","."}))return false;
  documentation["when_to_use"] =
      "Use after discovery confirms this capability is available and authorized.";
  documentation["when_not_to_use"] =
      "Do not bypass the declared schema, permission policy, or resource ownership.";
  documentation["side_effects"] = operation_id == nullptr
                                      ? "Operations may observe or change the named device domain."
                                      : "Side effects follow the operation kind and mutating flag.";
  JsonObject example = documentation["examples"].to<JsonArray>().add<JsonObject>();
  if (operation_id != nullptr) {
    example["operation"] = operation_id;
    example["arguments"].to<JsonObject>();
  } else {
    example["capability"] = JsonString(subject.data(),subject.size());
  }
  return !example.isNull();
}

void scan_lunarlink_capability(const DeviceModuleSpec &spec,void *opaque) {
  if(!spec.id||!spec.capability_id||!spec.operation_count)return;
  auto &context=*static_cast<LunarLinkManifestContext *>(opaque);
  const std::string_view module_id=spec.id;
  for(size_t i=0;i<spec.operation_count;++i)if(spec.operations[i].id) {
    const std::string_view id=spec.operations[i].id;
    context.has_project_lifecycle=context.has_project_lifecycle||id=="project.upload";
    context.has_project_declarative=context.has_project_declarative||id=="project.surface.get";
  }
  context.has_display=context.has_display||module_id=="media"||module_id.find("screen")!=std::string_view::npos||
      module_id.find("display")!=std::string_view::npos||module_id.find("eyes")!=std::string_view::npos;
  context.has_audio=context.has_audio||module_id=="assistant"||module_id.find("audio")!=std::string_view::npos;
  context.has_camera=context.has_camera||module_id=="media"||module_id.find("camera")!=std::string_view::npos||module_id.find("vision")!=std::string_view::npos;
  const bool motion=module_id=="skateboard"||module_id.find("motion")!=std::string_view::npos||module_id.find("motor")!=std::string_view::npos;
  context.has_motion=context.has_motion||motion;
  context.motion_available=context.motion_available||(motion&&context.registry.context().capabilities.active(spec.capability_id));
  context.has_network=context.has_network||module_id=="network";
  context.has_storage=context.has_storage||module_id=="filesystem"||module_id.find("storage")!=std::string_view::npos;
}
struct ModuleStreamContext {DeviceProjectionRegistry &registry;JsonStreamSink &sink;bool manifest=false,first=true,failed=false;};
bool fill_manifest_operation(JsonObject item,std::string_view module,const DeviceOperationSpec &operation) {
  item["id"]=operation.id;item["kind"]=lunarlink_operation_kind(operation);
  JsonObject permission=item["permission"].to<JsonObject>();
  permission["id"]=operation.mutating?"device.control":"device.read";
  const char *risk=lunarlink_risk(module,operation);permission["risk"]=risk;
  permission["confirmation"]=std::strcmp(risk,"read")==0?"none":"policy";
  item["cancellable"]=operation.cancellable;item["idempotency"]=operation.mutating?"optional":"not_applicable";
  if(!add_schema(item["input_schema"].to<JsonObject>(),operation.request_schema)||
     !add_schema(item["output_schema"].to<JsonObject>(),operation.response_schema))return false;
  return fill_documentation(item["documentation"].to<JsonObject>(),module,operation.id);
}
void stream_module(const DeviceModuleSpec &spec,void *opaque) {
  auto &context=*static_cast<ModuleStreamContext *>(opaque);
  if(context.failed||!spec.id||(context.manifest&&(!spec.capability_id||!spec.operation_count)))return;
  auto &sink=context.sink;auto &registry=context.registry;
  auto header=make_http_json_document();JsonObject module=header.to<JsonObject>();
  ModuleProjectionLookup projection{spec.id};registry.visit_modules(find_module_projection,&projection);
  if(context.manifest) {
    module["id"]=spec.capability_id;module["version"]=1;
    if(!set_joined_json_string(module,"provided_by",{"lunaros.",spec.id})){context.failed=true;return;}
    const bool available=registry.context().capabilities.active(spec.capability_id);module["available"]=available;
    if(!available)module["unavailable_reason"]=projection.descriptor&&projection.descriptor->module->unavailable_reason?
        projection.descriptor->module->unavailable_reason:"runtime.unavailable";
    if(!fill_documentation(module["documentation"].to<JsonObject>(),spec.id)){context.failed=true;return;}
  } else {
    module["id"]=spec.id;module["kind"]=spec.kind?spec.kind:spec.id;module["capability"]=spec.capability_id;
    if(projection.descriptor&&projection.descriptor->module->label)module["label"]=projection.descriptor->module->label;
    if(projection.descriptor&&projection.descriptor->module->fill_contract)
      projection.descriptor->module->fill_contract(module["contract"].to<JsonObject>());
  }
  if(header.overflowed()||(!context.first&&!sink.text(","))||!write_json_object_prefix(sink,header)) {context.failed=true;return;}
  // Compile-time UI JSON is streamed from read-only storage. Do not expand a
  // second large tree in the device's small HTTP workspace.
  if(!context.manifest&&projection.descriptor&&projection.descriptor->module->workbench_ui&&
     (!sink.text(",\"ui\":")||!sink.text(projection.descriptor->module->workbench_ui))) {context.failed=true;return;}
  if(!sink.text(",\"operations\":[")) {context.failed=true;return;}
  context.first=false;header.clear();bool first=true;
  for(size_t i=0;i<spec.operation_count;++i) {
    const auto &operation=spec.operations[i];if(!operation.id)continue;
    const auto *binding=context.manifest?nullptr:registry.find_operation(spec.id,operation.id);
    if(!context.manifest&&!binding)continue;
    auto fragment=make_http_json_document();JsonObject item=fragment.to<JsonObject>();
    if(context.manifest) {
      if(!fill_manifest_operation(item,spec.id,operation)){context.failed=true;return;}
    } else {
      item["id"]=operation.id;item["method"]=device_operation_method_name(binding->method);
      if(!set_joined_json_string(item,"path",{kDeviceModuleRoutePrefix,spec.id,"/",operation.id})){context.failed=true;return;}
      item["kind"]=device_operation_kind_name(operation.kind);item["mutating"]=operation.mutating;
      item["remote"]=operation.remote_access==DeviceRemoteAccess::Query?"query":operation.remote_access==DeviceRemoteAccess::Command?"command":"none";
      item["handler"]=device_operation_handler_kind_name(binding->handler_kind);
      item["response"]=operation.kind==DeviceOperationKind::Stream?"stream":"json";
      item["cancellable"]=operation.cancellable;
      // Only UI consumers need parameter schemas on this transport document.
      // Other modules retain their compact capability projection.
      if(projection.descriptor&&projection.descriptor->module->workbench_ui&&
         (!add_schema(item["input_schema"].to<JsonObject>(),operation.request_schema)||
          !add_schema(item["output_schema"].to<JsonObject>(),operation.response_schema))) {
        context.failed=true;return;
      }
    }
    if(fragment.overflowed()||(!first&&!sink.text(","))||!sink.json(fragment.as<JsonVariantConst>())){context.failed=true;return;}first=false;
  }
  if(!sink.text("]}"))context.failed=true;
}

void append_resource(JsonArray resources, const char *id, const char *kind,
                     const char *sharing, bool available) {
  JsonObject resource = resources.add<JsonObject>();
  resource["id"] = id;
  resource["kind"] = kind;
  resource["sharing"] = sharing;
  resource["state"] = available ? "available" : "unavailable";
}

std::string generated_at() {
  const std::time_t now = std::time(nullptr);
  std::tm utc{};
  gmtime_r(&now, &utc);
  char value[32]{};
  if (std::strftime(value, sizeof(value), "%Y-%m-%dT%H:%M:%SZ", &utc) == 0)
    return "1970-01-01T00:00:00Z";
  return value;
}

void find_module_projection(const DeviceModuleProjection &descriptor,
                            void *opaque) {
  auto &lookup = *static_cast<ModuleProjectionLookup *>(opaque);
  if (lookup.descriptor == nullptr && descriptor.module != nullptr &&
      descriptor.module->id != nullptr && lookup.id != nullptr &&
      std::strcmp(descriptor.module->id,lookup.id)==0) {
    lookup.descriptor = &descriptor;
  }
}


void handle_device(DeviceProjectionRegistry &registry) {
  const bool metadata_allowed =
      device_api_current_request_allowed(DeviceApiAccessSurface::Metadata,
          nullptr, request_header("X-Lunar-Debug-Token").c_str());
  auto response=make_http_json_document();
  response["apiVersion"] = kDeviceApiVersion;
  response["ok"] = true;
  JsonObject data = response["result"].to<JsonObject>();
  const bool bound = device_api_bound_access_required();
  data["access_required"] = bound;
  if (!metadata_allowed || (bound && !device_api_admin_access_authorized(
          request_header("X-Lunar-Debug-Token").c_str()) &&
      !device_api_bound_access_authorized(
          request_header("X-Lunar-Access-Grant").c_str()) &&
      !device_api_shared_access_authorized(request_header("X-Lunar-Access-Grant").c_str()))) {
    data["device"]["serial_number"] = hardware_serial_number();
    data["binding"]["state"] = bound ? "bound" : "unbound";
    serialize_and_send_json(200, response);
    return;
  }
  registry.fill_info_payload(data["device"].to<JsonObject>());
  // One image can boot first as an installer and later with the full product
  // composition. Registry generations restart with the process, so include a
  // boot nonce: an installer schema must not survive activation in a host cache.
  // Initialize after platform startup, once, rather than rebuilding the ELF hex
  // string on every live status read.
  data["capabilities_revision"] = registry.declaration_revision();
  // Immutable declarations survive reconnects and device reboots. Firmware ELF
  // content or a newly sealed composition changes the revision, not telemetry.
  data["declaration_cache"] = "firmware";
  registry.fill_status_sections(data["status"].to<JsonObject>());
  if (device_api_shared_access_authorized(request_header("X-Lunar-Access-Grant").c_str())) {
    auto safe_status=make_http_json_document();
    safe_status["desktop_pet"].set(data["status"]["desktop_pet"]);
    safe_status["controller"].set(data["status"]["controller"]);
    data["status"].set(safe_status.as<JsonObject>());
  }
  serialize_and_send_json(200, response);
}

void handle_capabilities(DeviceProjectionRegistry &registry) {
  if (!device_api_current_request_allowed(DeviceApiAccessSurface::Metadata,
          nullptr, request_header("X-Lunar-Debug-Token").c_str())) {
    send_device_api_error(403, "provisioning_surface_restricted");
    return;
  }
  send_segmented_json(200,[](JsonStreamSink &sink,void *opaque) {
    auto &owner=*static_cast<DeviceProjectionRegistry *>(opaque);
    if(!sink.text("{\"apiVersion\":\"1\",\"ok\":true,\"result\":{\"modules\":["))return false;
    ModuleStreamContext context{owner,sink};owner.context().models.visit(stream_module,&context);
    return !context.failed&&sink.text("]}}");
  },&registry);
}

void handle_lunarlink_manifest(DeviceProjectionRegistry &registry) {
  if (!device_api_current_request_allowed(DeviceApiAccessSurface::Metadata,
          nullptr, request_header("X-Lunar-Debug-Token").c_str())) {
    send_device_api_error(403, "provisioning_surface_restricted");
    return;
  }

  auto manifest=make_http_json_document();
  manifest["schema_version"] = "1.0.0";
  manifest["kind"] = "lunarlink.device_manifest";
  manifest["generated_at"] = generated_at();
  JsonObject protocol = manifest["protocol"].to<JsonObject>();
  protocol["name"] = std::string(lunaredu::protocol::contract::name);
  protocol["version"] = lunaredu::protocol::contract::version;
  protocol["family"] = std::string(lunaredu::protocol::contract::family);
  JsonArray features = protocol["features"].to<JsonArray>();
  features.add("activity.feedback");
  features.add("capability.describe");
  features.add("diagnostic.structured");
  features.add("media.audio");
  features.add("media.image");
  features.add("media.video");
  features.add("network.service");
  features.add("shared_access_v1");

  JsonObject raw_info = manifest["_info"].to<JsonObject>();
  registry.fill_info_payload(raw_info);
  JsonObject raw_status = manifest["_status"].to<JsonObject>();
  registry.fill_status_sections(raw_status);

  const char *serial = raw_info["serial_number"] | "unknown-device";
  const char *device_uuid = raw_info["device_uuid"] | serial;
  const char *product_id = raw_info["application"] |
                           raw_status["desktop_pet"]["firmware_key"] |
                           raw_info["board_type"] | "lunaros-device";
  const char *board_id = raw_info["board_id"] | raw_info["board_type"] |
                         "lunaros-board";
  const char *display_name = raw_info["device_name"] | raw_info["model"] |
                             "LunarOS device";
  const char *firmware = raw_info["fw_version"] | "0.0.0";
  JsonObject device = manifest["device"].to<JsonObject>();
  device["id"] = device_uuid;
  device["serial"] = serial;
  device["product_id"] = product_id;
  device["board_id"] = board_id;
  device["display_name"] = display_name;
  device["firmware_version"] = firmware;
  device["lunaros_version"] = "1.0.0";

  // Voice-assistant activation is separate from the account/device binding.
  // The manifest must report the same authoritative snapshot used by LAN grants.
  const auto account_binding = platform_link_pairing_snapshot();
  JsonObject binding = manifest["binding"].to<JsonObject>();
  binding["state"] = account_binding.state == PlatformLinkBindingState::Bound ||
                              !account_binding.account_id.empty()
                          ? "bound"
                          : "unbound";
  binding["epoch"] = account_binding.binding_epoch;

  JsonVariantConst lunarlink_status = raw_status["lunarlink"];

  LunarLinkManifestContext context{registry};
  registry.context().models.visit(scan_lunarlink_capability, &context);
  if (context.has_project_lifecycle) features.add("project.lifecycle");
  if (context.has_project_declarative) features.add("project.declarative");

  JsonArray resources = manifest["resources"].to<JsonArray>();
  const bool display_available =
      raw_status["desktop_pet"]["screen_ready"] | false;
  const bool camera_available =
      context.has_camera &&
      std::string(raw_status["desktop_pet"]["camera_error"] | "").empty();
  const bool audio_input_available =
      (lunarlink_status["audio_initialized"] | false) &&
      (lunarlink_status["microphone_enabled"] | false);
  const bool audio_output_available =
      lunarlink_status["audio_initialized"] | false;
  const bool network_available =
      raw_status["network"]["wifi"]["connected"] | false;
  const bool storage_available = raw_status["filesystem"]["ready"] | false;
  if (context.has_display)
    append_resource(resources, "display.primary", "display", "exclusive",
                    display_available);
  if (context.has_audio) {
    append_resource(resources, "audio.microphone", "audio.input", "exclusive",
                    audio_input_available);
    append_resource(resources, "audio.speaker", "audio.output", "exclusive",
                    audio_output_available);
  }
  if (context.has_camera)
    append_resource(resources, "camera.primary", "camera", "exclusive",
                    camera_available);
  if (context.has_motion)
    append_resource(resources, "motion.primary", "motion", "exclusive", context.motion_available);
  if (context.has_network)
    append_resource(resources, "network.internet", "network", "multiplexed",
                    network_available);
  if (context.has_storage)
    append_resource(resources, "storage.application", "storage", "shared",
                    storage_available);

  JsonArray diagnostics = manifest["diagnostics"].to<JsonArray>();
  JsonObject diagnostic = diagnostics.add<JsonObject>();
  diagnostic["id"] = "diagnostics.system";
  diagnostic["summary"] =
      "Inspect LunarOS services, capabilities, resources, and recent structured errors.";
  JsonObject permission = diagnostic["permission"].to<JsonObject>();
  permission["id"] = "diagnostics.read";
  permission["risk"] = "read";
  permission["confirmation"] = "none";

  manifest.remove("_info");
  manifest.remove("_status");
  if(manifest.overflowed()){serialize_and_send_json(503,manifest);return;}
  struct ManifestStream {DeviceProjectionRegistry &registry;JsonDocument &header;} stream{registry,manifest};
  send_segmented_json(200,[](JsonStreamSink &sink,void *opaque) {
    auto &owner=*static_cast<ManifestStream *>(opaque);
    if(!write_json_object_prefix(sink,owner.header)||!sink.text(",\"capabilities\":["))return false;
    owner.header.clear(); // Release status/header storage before any operation schema.
    ModuleStreamContext context{owner.registry,sink,true};owner.registry.context().models.visit(stream_module,&context);
    return !context.failed&&sink.text("]}");
  },&stream);
}

} // namespace

void register_device_model_routes(lunaredu::lunaros::DeviceProjectionRegistry &registry) {
  register_http_route("/api/v1/device", HTTP_GET, [&registry] { handle_device(registry); });
  register_http_route("/api/v1/capabilities", HTTP_GET, [&registry] { handle_capabilities(registry); });
  register_http_route("/api/v1/lunarlink/manifest", HTTP_GET,
                      [&registry] { handle_lunarlink_manifest(registry); });
}
