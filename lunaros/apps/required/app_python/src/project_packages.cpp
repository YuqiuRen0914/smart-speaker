#include "device_handlers.h"

#include <cctype>
#include <string>

#include <ArduinoJson.h>

#include "device_api/device_api_context.h"
#include "device_api/device_projection.h"
#include "device_api/composed_operation.h"
#include "project_records.h"
#include "script_catalog.h"
#include "lunaros_micropython_config.h"
#include "control_dispatch.h"
#include "script_name.h"
#include "lunaros_storage.h"

using namespace lunaredu::lunaros;

namespace {

constexpr const char *kProjectRecordRoot = "/managed/python-projects";
bool stable_id(const std::string &value) {
  if (value.empty() || value.size() > 96 ||
      value.front() < 'a' || value.front() > 'z')
    return false;
  bool separator = false;
  for (const unsigned char ch : value) {
    const bool alpha_numeric = (ch >= 'a' && ch <= 'z') ||
                               (ch >= '0' && ch <= '9');
    const bool current_separator = ch == '.' || ch == '_' || ch == '-';
    if (!alpha_numeric && !current_separator) return false;
    if (current_separator && separator) return false;
    separator = current_separator;
  }
  return !separator;
}

bool semantic_version(const std::string &value) {
  int dots = 0;
  bool digit = false;
  for (const unsigned char ch : value) {
    if (ch == '.') {
      if (!digit || ++dots > 2) return false;
      digit = false;
      continue;
    }
    if (!std::isdigit(ch)) return false;
    digit = true;
  }
  return dots == 2 && digit;
}

constexpr const char *kCurrentRoot="/managed/python-projects/v3/current";
constexpr const char *kPreviousRoot="/managed/python-projects/v3/previous";
constexpr size_t kMaximumRecordBytes=kMicroPythonMaxUploadPayloadBytes+4096;
std::string record_path(const std::string &id) { return std::string(kCurrentRoot)+"/"+id+".json"; }
std::string previous_record_path(const std::string &id) { return std::string(kPreviousRoot)+"/"+id+".json"; }
std::string legacy_path(const std::string &id,bool previous=false) {
  return std::string(kProjectRecordRoot)+"/"+id+(previous?".previous.json":".json");
}
bool prepare_record_root(std::string &error) {
  if(!storage_ready()){error="storage_not_ready";return false;}
  for(const char *path:{"/managed",kProjectRecordRoot,"/managed/python-projects/v3",kCurrentRoot,kPreviousRoot})
    if(!storage_path_exists(path)&&!storage_ensure_directory(path)){error="project_record_root_create_failed";return false;}
  return true;
}
bool decode_record(const std::string &path,JsonDocument &record,std::string &error) {
  std::string encoded;
  if(!storage_read_text_file_bounded(path.c_str(),kMaximumRecordBytes,encoded)||deserializeJson(record,encoded)){
    error="project_record_invalid";return false;
  }
  return true;
}
bool read_record(const std::string &id,bool previous,JsonDocument &record,std::string &error) {
  if(!storage_ready()){error="storage_not_ready";return false;}
  const auto current=record_path(id);
  // A tombstone shadows retained legacy files; deletion must not resurrect them.
  if(previous&&storage_path_exists(current.c_str())) {
    JsonDocument owner;
    if(!decode_record(current,owner,error))return false;
    if(owner["state"]=="deleted"){error="project_not_found";return false;}
  }
  const auto path=previous?previous_record_path(id):current;
  const bool modern=storage_path_exists(path.c_str());
  const auto selected=modern?path:legacy_path(id,previous);
  if(!storage_path_exists(selected.c_str())){error="project_not_found";return false;}
  if(!decode_record(selected,record,error))return false;
  if(std::string(record["manifest"]["app"]["id"]|"")!=id || record["state"]=="deleted") {
    // Legacy foo.previous may belong to project foo's rollback, not the
    // distinct project foo.previous. Never read, overwrite or delete its owner.
    error="project_not_found";return false;
  }
  error.clear();return true;
}
bool write_record(const std::string &path,JsonVariantConst manifest,
    const std::string &entry,const std::string &content,bool autorun,std::string &error) {
  JsonDocument record;record["record_version"]=3;record["manifest"].set(manifest);
  record["entry_name"]=entry;record["content"]=content;record["autorun"]=autorun;record["state"]="installed";
  std::string encoded;serializeJson(record,encoded);
  if(!storage_write_text_file(path.c_str(),encoded.c_str())){error="project_record_write_failed";return false;}
  return true;
}
struct RecordSlot {
  std::string path,content;bool existed=false;
  explicit RecordSlot(std::string value):path(std::move(value)){}
  bool capture(std::string &error) {
    existed=storage_path_exists(path.c_str());
    if(existed&&!storage_read_text_file_bounded(path.c_str(),kMaximumRecordBytes,content)){
      error="project_record_read_failed";return false;
    }
    return true;
  }
  bool restore() const {
    return existed?storage_write_text_file(path.c_str(),content.c_str()):
        (!storage_path_exists(path.c_str())||storage_delete_file(path.c_str()));
  }
};

DeviceOperationResult package_error(const std::string &error,
                                    uint16_t status = 400) {
  return device_operation_error(error.c_str(), status,
                                "lunarlink_app_operation_failed");
}

DeviceOperationResult activity_package_error(
    DeviceApiContext &device, JsonObject response, const std::string &activity_id,
    const std::string &error, const char *message, uint16_t status = 500) {
  (void)device.model.activities.fail(activity_id.c_str(), error.c_str(),
                                     message);
  // Mark the error response as owned by this activity. The generic operation
  // wrapper then preserves this activity instead of manufacturing a second
  // failure record for the same request.
  response["activity_id"] = activity_id;
  response["activity_state"] = "failed";
  response["ok"] = false;
  response["state"] = "failed";
  return package_error(error, status);
}

bool validate_manifest(JsonVariantConst manifest, const std::string &entry_name,
                       std::string &error) {
  if (!manifest.is<JsonObjectConst>() ||
      std::string(manifest["schema_version"] | "") != "1.0.0" ||
      std::string(manifest["kind"] | "") != "lunarlink.app_manifest") {
    error = "project_manifest_header_invalid";
    return false;
  }
  const JsonVariantConst app = manifest["app"];
  const std::string project_id = app["id"] | "";
  const std::string version = app["version"] | "";
  if (!stable_id(project_id) || !semantic_version(version) ||
      std::string(app["name"] | "").empty() ||
      std::string(app["description"] | "").empty()) {
    error = "project_identity_invalid";
    return false;
  }
  const std::string tier = app["tier"] | "";
  if (tier != "sandbox_script" && tier != "declarative") {
    error = "project_tier_not_supported";
    return false;
  }
  const bool entry_valid = tier == "sandbox_script"
                               ? lunaredu::python::scripts::valid_script_name(entry_name)
                               : stable_id(entry_name);
  if (!entry_valid || entry_name.size() > (tier == "sandbox_script" ? 24 : 64)) {
    error = "project_entry_name_invalid";
    return false;
  }
  const JsonVariantConst compatibility = manifest["compatibility"];
  if (std::string(compatibility["lunarlink"] | "") != "1.0.0" ||
      std::string(compatibility["lunaros"] | "") != "1.0.0" ||
      !compatibility["products"].is<JsonArrayConst>()) {
    error = "project_compatibility_invalid";
    return false;
  }
  if (!manifest["provenance"].is<JsonObjectConst>() ||
      !manifest["entrypoints"].is<JsonArrayConst>() ||
      manifest["entrypoints"].as<JsonArrayConst>().size() == 0 ||
      !manifest["capabilities"].is<JsonObjectConst>() ||
      !manifest["permissions"].is<JsonArrayConst>() ||
      !manifest["resources"].is<JsonArrayConst>() ||
      !manifest["assets"].is<JsonArrayConst>()) {
    error = "project_declarations_incomplete";
    return false;
  }
  const JsonVariantConst lifecycle = manifest["lifecycle"];
  if (!lifecycle.is<JsonObjectConst>() ||
      std::string(lifecycle["health_check"] | "").empty() ||
      std::string(lifecycle["rollback"] | "") == "not_supported") {
    error = "project_lifecycle_invalid";
    return false;
  }
  return true;
}

bool manifest_declares(JsonVariantConst manifest, const char *collection,
                       const std::string &id) {
  const JsonVariantConst items = manifest[collection];
  if (!items.is<JsonArrayConst>()) return false;
  for (JsonVariantConst item : items.as<JsonArrayConst>()) {
    if (std::string(item["id"] | "") == id) return true;
  }
  return false;
}

bool manifest_allows_operation(JsonVariantConst manifest,
                               const std::string &capability,
                               const std::string &operation) {
  const JsonVariantConst requirements = manifest["capabilities"]["requires"];
  if (!requirements.is<JsonArrayConst>()) return false;
  for (JsonVariantConst requirement : requirements.as<JsonArrayConst>()) {
    if (std::string(requirement["id"] | "") != capability ||
        !requirement["operations"].is<JsonArrayConst>())
      continue;
    for (JsonVariantConst allowed : requirement["operations"].as<JsonArrayConst>()) {
      if (std::string(allowed | "") == operation) return true;
    }
  }
  return false;
}

bool array_has_unique_stable_ids(JsonArrayConst items) {
  for (size_t index = 0; index < items.size(); ++index) {
    const std::string id = items[index]["id"] | "";
    if (!stable_id(id)) return false;
    for (size_t other = index + 1; other < items.size(); ++other) {
      if (id == std::string(items[other]["id"] | "")) return false;
    }
  }
  return true;
}

bool array_declares_id(JsonArrayConst items, const std::string &id) {
  for (JsonVariantConst item : items) {
    if (std::string(item["id"] | "") == id) return true;
  }
  return false;
}

JsonVariantConst resolve_binding_path(JsonVariantConst root,
                                      const std::string &path) {
  if (path.empty()) return root;
  size_t offset = 0;
  JsonVariantConst current = root;
  while (offset < path.size() && !current.isNull()) {
    const size_t separator = path.find('.', offset);
    const std::string token = path.substr(
        offset, separator == std::string::npos ? std::string::npos
                                                : separator - offset);
    current = current[token.c_str()];
    if (separator == std::string::npos) break;
    offset = separator + 1;
  }
  return current;
}

bool validate_declarative_document(DeviceApiContext &device,
                                   JsonVariantConst manifest,
                                   const std::string &content,
                                   JsonDocument &document,
                                   std::string &error) {
  if (deserializeJson(document, content) != DeserializationError::Ok ||
      std::string(document["schema_version"] | "") != "1.0.0" ||
      std::string(document["kind"] | "") != "lunarlink.declarative_app" ||
      !document["surface"].is<JsonObjectConst>() ||
      std::string(document["surface"]["title"] | "").empty() ||
      std::string(document["surface"]["description"] | "").empty() ||
      !document["actions"].is<JsonArrayConst>()) {
    error = "declarative_project_document_invalid";
    return false;
  }
  const JsonArrayConst actions = document["actions"].as<JsonArrayConst>();
  if (actions.size() == 0 || actions.size() > 16 ||
      !array_has_unique_stable_ids(actions)) {
    error = "declarative_project_actions_invalid";
    return false;
  }
  const JsonVariantConst surface = document["surface"];
  const std::string layout = surface["layout"] | "stack";
  if (layout != "stack" && layout != "grid") {
    error = "declarative_project_surface_layout_invalid";
    return false;
  }
  JsonArrayConst bindings;
  if (!document["bindings"].isNull()) {
    if (!document["bindings"].is<JsonArrayConst>()) {
      error = "declarative_project_bindings_invalid";
      return false;
    }
    bindings = document["bindings"].as<JsonArrayConst>();
    if (bindings.size() > 16 || !array_has_unique_stable_ids(bindings)) {
      error = "declarative_project_bindings_invalid";
      return false;
    }
  }
  auto *registry = device.projections();
  if (registry == nullptr) {
    error = "device_projection_registry_unavailable";
    return false;
  }
  for (JsonVariantConst action : actions) {
    const std::string id = action["id"] | "";
    const std::string module = action["module"] | "";
    const std::string capability = action["capability"] | "";
    const std::string operation = action["operation"] | "";
    const std::string permission = action["permission"] | "";
    const std::string resource = action["resource"] | "";
    const std::string trigger = action["trigger"] | "mount";
    if (!stable_id(id) || !stable_id(module) || !stable_id(capability) ||
        !stable_id(operation) || !action["payload"].is<JsonObjectConst>() ||
        (trigger != "mount" && trigger != "event")) {
      error = "declarative_project_action_invalid";
      return false;
    }
    if (!manifest_allows_operation(manifest, capability, operation)) {
      error = "declarative_project_capability_not_declared:" + capability + ":" + operation;
      return false;
    }
    if ((!permission.empty() && !manifest_declares(manifest, "permissions", permission)) ||
        (!resource.empty() && !manifest_declares(manifest, "resources", resource))) {
      error = "declarative_project_access_not_declared:" + id;
      return false;
    }
    const auto target=check_composed_device_operation(device,
        {module.c_str(),operation.c_str(),capability.c_str(),true,"micropython"},action["payload"]);
    if(!target.ok){error=target.error;return false;}

  }
  for (JsonVariantConst binding : bindings) {
    const std::string id = binding["id"] | "";
    const std::string module = binding["module"] | "";
    const std::string capability = binding["capability"] | "";
    const std::string operation = binding["operation"] | "";
    const std::string path = binding["path"] | "";
    const int refresh_ms = binding["refresh_ms"] | 2000;
    if (!stable_id(id) || !stable_id(module) || !stable_id(capability) ||
        !stable_id(operation) || path.size() > 96 || refresh_ms < 500 ||
        refresh_ms > 60000 ||
        !manifest_allows_operation(manifest, capability, operation)) {
      error = "declarative_project_binding_invalid:" + id;
      return false;
    }
    const auto target=check_composed_device_operation(device,
        {module.c_str(),operation.c_str(),capability.c_str(),false,"micropython"});
    if(!target.ok){error=target.error;return false;}

  }
  if (!surface["components"].isNull()) {
    if (!surface["components"].is<JsonArrayConst>()) {
      error = "declarative_project_components_invalid";
      return false;
    }
    const JsonArrayConst components = surface["components"].as<JsonArrayConst>();
    if (components.size() > 24 || !array_has_unique_stable_ids(components)) {
      error = "declarative_project_components_invalid";
      return false;
    }
    for (JsonVariantConst component : components) {
      const std::string id = component["id"] | "";
      const std::string type = component["type"] | "";
      const std::string label = component["label"] | "";
      const std::string text = component["text"] | "";
      const std::string binding_id = component["binding_id"] | "";
      const std::string action_id = component["action_id"] | "";
      const bool valid =
          (type == "text" && !text.empty() && label.empty() &&
           binding_id.empty() && action_id.empty()) ||
          ((type == "value" || type == "badge") && !label.empty() &&
           array_declares_id(bindings, binding_id) && action_id.empty() &&
           text.empty()) ||
          (type == "button" && !label.empty() &&
           array_declares_id(actions, action_id) && binding_id.empty() &&
           text.empty());
      if (!stable_id(id) || !valid) {
        error = "declarative_project_component_invalid:" + id;
        return false;
      }
    }
  }
  return true;
}

DeviceOperationResult invoke_declarative_action(
    DeviceApiContext &device, JsonVariantConst action, JsonObject output) {
  const std::string module=action["module"]|"";
  const std::string operation=action["operation"]|"";
  const std::string capability=action["capability"]|"";
  return invoke_composed_device_operation(device,
      {module.c_str(),operation.c_str(),capability.c_str(),true,"micropython"},action["payload"],output);
}

bool fill_declarative_surface(DeviceApiContext &device, JsonVariantConst document,
                              JsonObject response, std::string &error) {
  response["surface"].set(document["surface"]);
  JsonArray resolved = response["bindings"].to<JsonArray>();
  if (!document["bindings"].is<JsonArrayConst>()) return true;
  for (JsonVariantConst binding : document["bindings"].as<JsonArrayConst>()) {
    const std::string module = binding["module"] | "";
    const std::string operation = binding["operation"] | "";
    const std::string capability=binding["capability"]|"";
    JsonObject item = resolved.add<JsonObject>();
    item["id"] = binding["id"];
    item["refresh_ms"] = binding["refresh_ms"] | 2000;
    JsonObject data = item["data"].to<JsonObject>();
    const DeviceOperationResult outcome = invoke_composed_device_operation(device,
        {module.c_str(),operation.c_str(),capability.c_str(),false,"micropython"},JsonVariantConst{},data);
    item["ok"] = outcome.ok;
    if (!outcome.ok) {
      error = outcome.error.empty() ? "declarative_project_binding_failed"
                                    : outcome.error;
      return false;
    }
    const std::string path = binding["path"] | "";
    const JsonVariantConst value = resolve_binding_path(data, path);
    if (!path.empty() && value.isNull()) {
      error = "declarative_project_binding_path_missing:" +
              std::string(binding["id"] | "");
      return false;
    }
    item["value"].set(value);
  }
  return true;
}

bool run_declarative_document(DeviceApiContext &device, JsonVariantConst manifest,
                              const std::string &content, JsonArray results,
                              std::string &error) {
  JsonDocument document;
  if (!validate_declarative_document(device, manifest, content, document, error))
    return false;
  for (JsonVariantConst action : document["actions"].as<JsonArrayConst>()) {
    if (std::string(action["trigger"] | "mount") != "mount") continue;
    const std::string module = action["module"] | "";
    const std::string operation = action["operation"] | "";
    JsonObject item = results.add<JsonObject>();
    item["id"] = action["id"];
    item["module"] = module;
    item["operation"] = operation;
    JsonObject output = item["output"].to<JsonObject>();
    const DeviceOperationResult outcome =
        invoke_declarative_action(device, action, output);
    item["ok"] = outcome.ok;
    if (!outcome.ok) {
      error = outcome.error.empty() ? "declarative_project_action_failed" : outcome.error;
      return false;
    }
  }
  return true;
}

bool run_record(DeviceApiContext &device, JsonVariantConst record,
                JsonObject response, MicroPythonRuntimeStatus &status,
                std::string &error) {
  const std::string tier = record["manifest"]["app"]["tier"] | "sandbox_script";
  const std::string entry_name = record["entry_name"] | "";
  if (tier == "declarative") {
    const std::string content = record["content"] | "";
    return run_declarative_document(
        device, record["manifest"], content,
        response["actions"].to<JsonArray>(), error);
  }
  const std::string content=record["content"]|"";
  const std::string id=record["manifest"]["app"]["id"]|"";
  if(!validate_manifest(record["manifest"],entry_name,error)||content.empty()) {
    if(error.empty())error="project_legacy_content_missing";
    return false;
  }
  return dispatch_micropython_run_project_source(device.runtime,content,
      std::string("project://")+id+"/"+entry_name,&status,error);
}

using RecordVisitor=bool (*)(JsonVariantConst,void *);
bool visit_current_records(RecordVisitor visitor,void *opaque,std::string &error,bool prepare=true) {
  if(prepare) {if(!prepare_record_root(error))return false;}
  else if(!storage_ready()){error="storage_not_ready";return false;}
  for(const char *root:{kCurrentRoot,kProjectRecordRoot}) {
    if(!storage_path_exists(root))continue;
    struct Visit {RecordVisitor visitor;void *opaque;bool modern;const char *root;} visit{visitor,opaque,root==kCurrentRoot,root};
    if(!storage_list_entries(root,[](const StorageListEntry &entry,void *raw) {
      auto &v=*static_cast<Visit *>(raw);
      if(entry.is_directory)return true;
      const std::string prefix=std::string(v.root)+"/";
      if(entry.path.rfind(prefix,0)!=0)return true;
      const auto name=entry.path.substr(prefix.size());
      if(name.find('/')!=std::string::npos||name.size()<6||name.compare(name.size()-5,5,".json"))return true;
      const auto id=name.substr(0,name.size()-5);
      if(!stable_id(id)||(!v.modern&&storage_path_exists(record_path(id).c_str())))return true;
      JsonDocument record;std::string ignored;
      if(!read_record(id,false,record,ignored))return true;
      return v.visitor(record.as<JsonVariantConst>(),v.opaque);
    },&visit)){error="project_list_failed";return false;}
  }
  return true;
}

bool legacy_selection_project(const std::string &selection,std::string &id,
    JsonDocument *selected,std::string &error) {
  struct Match {const std::string &entry;std::string &id;JsonDocument *record;size_t count=0;}
      match{selection,id,selected};
  id.clear();
  if(!visit_current_records([](JsonVariantConst record,void *raw) {
    auto &m=*static_cast<Match *>(raw);
    if((record["autorun"]|false)&&std::string(record["entry_name"]|"")==m.entry&&
        std::string(record["manifest"]["app"]["tier"]|"")=="sandbox_script") {
      ++m.count;m.id=record["manifest"]["app"]["id"]|"";
      if(m.record)m.record->set(record);
    }
    return true;
  },&match,error,false))return false;
  if(match.count>1){error="project_legacy_autorun_ambiguous";return false;}
  return true;
}

bool selection_points_to_project(const std::string &selection,const std::string &id,
    JsonVariantConst current,bool &selected,std::string &error) {
  const std::string prefix=lunaredu::python::scripts::kProjectAutorunPrefix;
  selected=selection==prefix+id;
  if(selected||selection.rfind(prefix,0)==0||!(current["autorun"]|false)||
      selection!=std::string(current["entry_name"]|""))return true;
  std::string legacy_id;
  if(!legacy_selection_project(selection,legacy_id,nullptr,error))return false;
  selected=legacy_id==id;return true;
}

void fill_install_response(JsonObject response, JsonVariantConst manifest,
                           const std::string &entry_name,
                           const MicroPythonRuntimeStatus &status,
                           bool rollback_available,
                           const std::string &activity_id) {
  response["ok"] = true;
  response["state"] = "succeeded";
  response["activity_id"] = activity_id;
  response["project_id"] = manifest["app"]["id"];
  response["version"] = manifest["app"]["version"];
  response["entry_name"] = entry_name;
  response["rollback_available"] = rollback_available;
  JsonArray checks = response["validation"]["passed"].to<JsonArray>();
  checks.add("manifest.structure");
  checks.add("compatibility.lunarlink");
  checks.add("compatibility.lunaros");
  checks.add(std::string(manifest["app"]["tier"] | "") == "declarative"
                 ? "entry.declarative"
                 : "entry.sandboxed");
  checks.add("lifecycle.rollback");
  if (std::string(manifest["app"]["tier"] | "") == "sandbox_script")
    fill_micropython_runtime_status_payload(
        response["runtime"].to<JsonObject>(), status);
}

} // namespace

DeviceOperationResult handle_micropython_project_upload_bulk_payload(
    DeviceApiContext &device, const DeviceBulkRequest &request,
    JsonObject response_payload) {
  if (request.body_size > kMicroPythonMaxUploadPayloadBytes)
    return package_error("project_package_too_large", 413);

  const JsonVariantConst manifest = request.payload["manifest"];
  const std::string entry_name = request.payload["entry_name"] | "";
  const std::string content = request.payload["content"] | "";
  const bool autorun = request.payload["autorun"] | false;
  const bool run = request.payload["run"] | false;
  std::string error;
  if (!validate_manifest(manifest, entry_name, error))
    return package_error(error);
  if (content.empty()) return package_error("project_content_empty");
  if (!prepare_record_root(error)) return package_error(error, 503);

  const std::string project_id = manifest["app"]["id"] | "";
  const std::string tier = manifest["app"]["tier"] | "";
  if (tier == "declarative") {
    JsonDocument declarative;
    if (!validate_declarative_document(device, manifest, content, declarative,
                                       error))
      return package_error(error);
  }
  const std::string activity_id = device.model.activities.accept(
      "micropython_runtime", "project.upload", "uploading Python project");
  if (activity_id.empty()) return package_error("activity_capacity_exhausted", 503);
  (void)device.model.activities.start(activity_id.c_str(),
                                      "validating and storing project package");
  JsonDocument old_record;
  const bool had_record=read_record(project_id,false,old_record,error);
  if(!had_record&&error!="project_not_found")return activity_package_error(device,response_payload,activity_id,error,"current project record could not be read");
  error.clear();
  RecordSlot current_slot(record_path(project_id)),previous_slot(previous_record_path(project_id));
  if(!current_slot.capture(error)||!previous_slot.capture(error))
    return activity_package_error(device,response_payload,activity_id,error,"project transaction snapshot failed");
  std::string old_selection;
  if(!lunaredu::python::scripts::autorun(old_selection,error))
    return activity_package_error(device,response_payload,activity_id,error,"autorun selection could not be read");
  bool was_selected=false;
  if(!selection_points_to_project(old_selection,project_id,old_record.as<JsonVariantConst>(),was_selected,error))
    return activity_package_error(device,response_payload,activity_id,error,"legacy autorun selection is ambiguous");
  const auto restore=[&]() {
    const bool current=current_slot.restore();const bool previous=previous_slot.restore();
    return current&&previous;
  };
  if(had_record) {
    std::string previous;serializeJson(old_record,previous);
    if(!storage_write_text_file(previous_slot.path.c_str(),previous.c_str()))
      return activity_package_error(device,response_payload,activity_id,"project_previous_record_write_failed","previous project record could not be stored");
  } else if(storage_path_exists(previous_slot.path.c_str())&&!storage_delete_file(previous_slot.path.c_str())) {
    return activity_package_error(device,response_payload,activity_id,"project_previous_record_remove_failed","stale rollback could not be cleared");
  }
  if(!write_record(current_slot.path,manifest,entry_name,content,autorun,error)) {
    if(!restore())error="project_transaction_restore_failed";
    return activity_package_error(device,response_payload,activity_id,error,"project record write failed");
  }
  // Commit every persistent change before admitting execution. A rejected
  // run restores both record slots and the previous standalone/project selector.
  const bool deselect=!autorun&&was_selected;
  const bool selection_changed=autorun||deselect;
  if(selection_changed&&!lunaredu::python::scripts::set_project_autorun(autorun?project_id:std::string(),error)) {
    if(!restore())error="project_transaction_restore_failed";
    return activity_package_error(device,response_payload,activity_id,error,"project autorun selection failed");
  }
  MicroPythonRuntimeStatus status;
  if(run) {
    JsonDocument installed;installed["manifest"].set(manifest);installed["entry_name"]=entry_name;installed["content"]=content;
    if(!run_record(device,installed.as<JsonVariantConst>(),response_payload,status,error)) {
      std::string selection_error;
      const bool selected=!selection_changed||lunaredu::python::scripts::restore_autorun_selection(old_selection,selection_error);
      if(!restore()||!selected)error="project_transaction_restore_failed";
      return activity_package_error(device,response_payload,activity_id,error,"project run failed; package restored");
    }
  }

  const std::string result_json =
      std::string("{\"project_id\":\"") + project_id + "\",\"version\":\"" +
      std::string(manifest["app"]["version"] | "") + "\"}";
  (void)device.model.activities.succeed(activity_id.c_str(), result_json.c_str(),
                                        "Project uploaded");
  fill_install_response(response_payload, manifest, entry_name, status,
                        had_record,
                        activity_id);
  return device_operation_ack(201);
}

DeviceOperationResult handle_micropython_project_preview(
    DeviceApiContext &device, JsonVariantConst request_payload,
    JsonObject response_payload) {
  const JsonVariantConst manifest = request_payload["manifest"];
  const std::string entry_name = request_payload["entry_name"] | "";
  const std::string content = request_payload["content"] | "";
  std::string error;
  if (!validate_manifest(manifest, entry_name, error)) return package_error(error);
  if (content.empty()) return package_error("project_content_empty");
  const std::string tier = manifest["app"]["tier"] | "";
  JsonDocument document;
  if (tier == "declarative" &&
      !validate_declarative_document(device, manifest, content, document, error))
    return package_error(error);

  const std::string activity_id = device.model.activities.accept(
      "micropython_runtime", "project.preview", "validating Python project preview");
  if (activity_id.empty()) return package_error("activity_capacity_exhausted", 503);
  (void)device.model.activities.start(activity_id.c_str(),
                                      "checking manifest and device targets");
  response_payload["ok"] = true;
  response_payload["state"] = "succeeded";
  response_payload["activity_id"] = activity_id;
  response_payload["project_id"] = manifest["app"]["id"];
  response_payload["version"] = manifest["app"]["version"];
  response_payload["tier"] = tier;
  JsonArray checks = response_payload["checks"].to<JsonArray>();
  checks.add("manifest.structure");
  checks.add("manifest.compatibility");
  checks.add("manifest.access_declarations");
  if (tier == "declarative") {
    checks.add("document.structure");
    checks.add("surface.components");
    checks.add("bindings.available");
    checks.add("actions.available");
    JsonObject surface_preview = response_payload["surface_preview"].to<JsonObject>();
    if (!fill_declarative_surface(device, document, surface_preview, error)) {
      return activity_package_error(device, response_payload, activity_id,
                                    error, "project surface preview failed", 409);
    }
    JsonArray plan = response_payload["plan"].to<JsonArray>();
    for (JsonVariantConst action : document["actions"].as<JsonArrayConst>()) {
      JsonObject item = plan.add<JsonObject>();
      item["id"] = action["id"];
      item["capability"] = action["capability"];
      item["module"] = action["module"];
      item["operation"] = action["operation"];
      item["trigger"] = action["trigger"] | "mount";
    }
  } else {
    checks.add("entry.sandboxed");
  }
  (void)device.model.activities.succeed(
      activity_id.c_str(), R"json({"valid":true})json", "project preview valid");
  return device_operation_ack();
}

DeviceOperationResult handle_micropython_project_surface_get(
    DeviceApiContext &device, JsonVariantConst request_payload,
    JsonObject response_payload) {
  const std::string project_id = request_payload["project_id"] | "";
  if (!stable_id(project_id)) return package_error("project_id_invalid");
  std::string error;
  JsonDocument record;
  if (!read_record(project_id,false,record,error))
    return package_error(error, 404);
  if (std::string(record["manifest"]["app"]["tier"] | "") !=
      "declarative")
    return package_error("project_surface_not_declarative", 409);
  JsonDocument document;
  const std::string content = record["content"] | "";
  if (!validate_declarative_document(device, record["manifest"], content,
                                     document, error))
    return package_error(error, 409);
  response_payload["ok"] = true;
  response_payload["project_id"] = project_id;
  response_payload["version"] = record["manifest"]["app"]["version"];
  if (!fill_declarative_surface(device, document, response_payload, error))
    return package_error(error, 409);
  return device_operation_ack();
}

DeviceOperationResult handle_micropython_project_action_invoke(
    DeviceApiContext &device, JsonVariantConst request_payload,
    JsonObject response_payload) {
  const std::string project_id = request_payload["project_id"] | "";
  const std::string action_id = request_payload["action_id"] | "";
  if (!stable_id(project_id) || !stable_id(action_id))
    return package_error("project_action_identity_invalid");
  std::string error;
  JsonDocument record;
  if (!read_record(project_id,false,record,error))
    return package_error(error, 404);
  if (std::string(record["manifest"]["app"]["tier"] | "") !=
      "declarative")
    return package_error("project_action_not_declarative", 409);
  JsonDocument document;
  const std::string content = record["content"] | "";
  if (!validate_declarative_document(device, record["manifest"], content,
                                     document, error))
    return package_error(error, 409);
  JsonVariantConst selected;
  for (JsonVariantConst action : document["actions"].as<JsonArrayConst>()) {
    if (std::string(action["id"] | "") == action_id) {
      selected = action;
      break;
    }
  }
  if (selected.isNull()) return package_error("project_action_not_found", 404);
  if (std::string(selected["trigger"] | "mount") != "event")
    return package_error("project_action_not_user_invocable", 409);

  const std::string activity_id = device.model.activities.accept(
      "micropython_runtime", "project.action.invoke",
      "invoking declarative project action");
  if (activity_id.empty()) return package_error("activity_capacity_exhausted", 503);
  (void)device.model.activities.start(activity_id.c_str(),
                                      "dispatching declared device action");
  JsonObject output = response_payload["output"].to<JsonObject>();
  const DeviceOperationResult outcome =
      invoke_declarative_action(device, selected, output);
  if (!outcome.ok) {
    error = outcome.error.empty() ? "declarative_project_action_failed"
                                  : outcome.error;
    return activity_package_error(device, response_payload, activity_id,
                                  error, "declarative project action failed",
                                  outcome.status_code == 0
                                      ? 500
                                      : outcome.status_code);
  }
  response_payload["ok"] = true;
  response_payload["state"] = "succeeded";
  response_payload["activity_id"] = activity_id;
  response_payload["project_id"] = project_id;
  response_payload["action_id"] = action_id;
  std::string result_json;
  serializeJson(output, result_json);
  (void)device.model.activities.succeed(
      activity_id.c_str(), result_json.c_str(), "project action completed");
  return device_operation_ack();
}

DeviceOperationResult handle_micropython_project_run(
    DeviceApiContext &device, JsonVariantConst request_payload,
    JsonObject response_payload) {
  const std::string project_id = request_payload["project_id"] | "";
  if (!stable_id(project_id)) return package_error("project_id_invalid");
  std::string error;
  JsonDocument record;
  if (!read_record(project_id,false,record,error))
    return package_error(error, 404);
  const std::string activity_id = device.model.activities.accept(
      "micropython_runtime", "project.run", "running Python project");
  if (activity_id.empty()) return package_error("activity_capacity_exhausted", 503);
  (void)device.model.activities.start(activity_id.c_str(), "executing project entrypoint");
  MicroPythonRuntimeStatus status;
  if (!run_record(device, record.as<JsonVariantConst>(), response_payload,
                  status, error)) {
    return activity_package_error(device, response_payload, activity_id,
                                  error, "project execution failed");
  }
  response_payload["ok"] = true;
  response_payload["state"] = "succeeded";
  response_payload["activity_id"] = activity_id;
  response_payload["project_id"] = project_id;
  response_payload["version"] = record["manifest"]["app"]["version"];
  response_payload["entry_name"] = record["entry_name"];
  if (std::string(record["manifest"]["app"]["tier"] | "") == "sandbox_script")
    fill_micropython_runtime_status_payload(
        response_payload["runtime"].to<JsonObject>(), status);
  (void)device.model.activities.succeed(
      activity_id.c_str(), R"json({"ran":true})json", "project run completed");
  return device_operation_ack();
}

DeviceOperationResult fill_micropython_project_list_payload(
    DeviceApiContext &, JsonObject response_payload) {
  response_payload["ok"]=true;
  JsonArray projects=response_payload["projects"].to<JsonArray>();
  std::string error;
  if(!visit_current_records([](JsonVariantConst record,void *opaque) {
    auto &projects=*static_cast<JsonArray *>(opaque);auto item=projects.add<JsonObject>();
    item["project"].set(record["manifest"]["app"]);item["entry_name"]=record["entry_name"];
    item["autorun"]=record["autorun"];item["state"]=record["state"];return true;
  },&projects,error))return package_error(error,500);
  response_payload["count"]=projects.size();return {};
}

DeviceOperationResult handle_micropython_project_rollback(
    DeviceApiContext &device, JsonVariantConst request_payload,
    JsonObject response_payload) {
  const std::string project_id = request_payload["project_id"] | "";
  if (!stable_id(project_id)) return package_error("project_id_invalid");
  std::string error;
  JsonDocument current;
  JsonDocument previous;
  if (!read_record(project_id,false,current,error) ||
      !read_record(project_id,true,previous,error))
    return package_error(error, 404);
  const std::string entry_name = previous["entry_name"] | "";
  const std::string tier =
      previous["manifest"]["app"]["tier"] | "sandbox_script";
  const std::string previous_content=previous["content"]|"";
  if(previous_content.empty())return package_error("project_legacy_content_missing",409);
  if(!validate_manifest(previous["manifest"],entry_name,error))return package_error(error,409);
  if(tier=="declarative") {
    JsonDocument document;
    if(!validate_declarative_document(device,previous["manifest"],previous_content,document,error))return package_error(error,409);
  }
  MicroPythonRuntimeStatus status;
  const std::string activity_id = device.model.activities.accept(
      "micropython_runtime", "project.rollback", "rolling back Python project");
  if (activity_id.empty()) return package_error("activity_capacity_exhausted", 503);
  (void)device.model.activities.start(activity_id.c_str(),
                                      "restoring previous project package");
  RecordSlot current_slot(record_path(project_id));
  if(!current_slot.capture(error))return activity_package_error(device,response_payload,activity_id,error,"rollback snapshot failed");
  std::string selection;
  if(!lunaredu::python::scripts::autorun(selection,error))
    return activity_package_error(device,response_payload,activity_id,error,"rollback autorun selection could not be read");
  bool was_selected=false;
  if(!selection_points_to_project(selection,project_id,current.as<JsonVariantConst>(),was_selected,error))
    return activity_package_error(device,response_payload,activity_id,error,"legacy autorun selection is ambiguous");
  if(!write_record(current_slot.path,previous["manifest"],entry_name,previous_content,previous["autorun"]|false,error))
    return activity_package_error(device,response_payload,activity_id,error,"previous project record could not be restored");
  const bool select=(previous["autorun"]|false)||was_selected;
  if(select&&!lunaredu::python::scripts::set_project_autorun((previous["autorun"]|false)?project_id:std::string(),error)) {
    if(!current_slot.restore())error="project_transaction_restore_failed";
    return activity_package_error(device,response_payload,activity_id,error,"rollback autorun selection failed");
  }
  if((request_payload["run"]|true)&&!run_record(device,previous.as<JsonVariantConst>(),response_payload,status,error)) {
    std::string selection_error;
    const bool selected=!select||lunaredu::python::scripts::restore_autorun_selection(selection,selection_error);
    if(!current_slot.restore()||!selected)error="project_transaction_restore_failed";
    return activity_package_error(device,response_payload,activity_id,error,"rolled back project failed to run; current restored");
  }

  response_payload["ok"] = true;
  response_payload["state"] = "succeeded";
  response_payload["activity_id"] = activity_id;
  response_payload["project_id"] = project_id;
  response_payload["version"] = previous["manifest"]["app"]["version"];
  response_payload["entry_name"] = entry_name;
  if (tier == "sandbox_script")
    fill_micropython_runtime_status_payload(
        response_payload["runtime"].to<JsonObject>(), status);
  (void)device.model.activities.succeed(
      activity_id.c_str(), R"json({"rolled_back":true})json",
      "project rollback completed");
  return device_operation_ack();
}

DeviceOperationResult handle_micropython_project_delete(
    DeviceApiContext &device, JsonVariantConst request_payload,
    JsonObject response_payload) {
  const std::string project_id = request_payload["project_id"] | "";
  if (!stable_id(project_id)) return package_error("project_id_invalid");
  std::string error;
  JsonDocument current;
  if (!read_record(project_id,false,current,error))
    return package_error(error, 404);
  const std::string activity_id = device.model.activities.accept(
      "micropython_runtime", "project.delete", "deleting Python project");
  if (activity_id.empty()) return package_error("activity_capacity_exhausted", 503);
  (void)device.model.activities.start(activity_id.c_str(),
                                      "removing project package and assets");
  std::string selection;bool was_selected=false;
  if(!lunaredu::python::scripts::autorun(selection,error)||
      !selection_points_to_project(selection,project_id,current.as<JsonVariantConst>(),was_selected,error))
    return activity_package_error(device,response_payload,activity_id,error,"project autorun selection could not be resolved");
  // The VM already owns a source snapshot. Cancel only this project's exact
  // execution, never the shared interpreter's unrelated script or REPL.
  const auto running=micropython_runtime_status();
  const auto prefix=std::string("project://")+project_id+"/";
  if(running.running&&running.current_program.rfind(prefix,0)==0&&
      !micropython_stop_execution(running.execution_id,error))
    return activity_package_error(device,response_payload,activity_id,error,"project cancellation failed");
  RecordSlot current_slot(record_path(project_id));
  if(!current_slot.capture(error))return activity_package_error(device,response_payload,activity_id,error,"delete snapshot failed");
  JsonDocument tombstone;tombstone["record_version"]=3;tombstone["state"]="deleted";
  tombstone["manifest"]["app"]["id"]=project_id;
  std::string encoded;serializeJson(tombstone,encoded);
  if(!storage_write_text_file(current_slot.path.c_str(),encoded.c_str()))
    return activity_package_error(device,response_payload,activity_id,"project_record_delete_failed","project tombstone could not be stored");
  if(was_selected&&!lunaredu::python::scripts::set_project_autorun("",error)) {
    if(!current_slot.restore())error="project_transaction_restore_failed";
    return activity_package_error(device,response_payload,activity_id,error,"project autorun removal failed");
  }
  // Retained legacy files may alias another project's rollback. The small v3
  // tombstone prevents fallback; shared /scripts and legacy files stay untouched.
  if(storage_path_exists(previous_record_path(project_id).c_str())&&
      !storage_delete_file(previous_record_path(project_id).c_str()))
    response_payload["cleanup_pending"]=true;
  response_payload["ok"] = true;
  response_payload["state"] = "succeeded";
  response_payload["activity_id"] = activity_id;
  response_payload["project_id"] = project_id;
  response_payload["data_retained"] = false;
  (void)device.model.activities.succeed(
      activity_id.c_str(), R"json({"deleted":true})json",
      "Project deleted");
  return device_operation_ack();
}

namespace lunaredu::python {
bool project_boot_source(const std::string &selection,std::string &source,
    std::string &program,bool &found,std::string &error) {
  found=false;source.clear();program.clear();error.clear();
  JsonDocument selected;
  const std::string prefix=scripts::kProjectAutorunPrefix;
  if(selection.rfind(prefix,0)==0) {
    found=true;const auto id=selection.substr(prefix.size());
    if(!stable_id(id)){error="project_id_invalid";return false;}
    if(!read_record(id,false,selected,error))return false;
  } else {
    std::string id;
    if(!legacy_selection_project(selection,id,&selected,error))return false;
    if(id.empty())return true;
    found=true;
  }
  const std::string id=selected["manifest"]["app"]["id"]|"";
  const std::string entry=selected["entry_name"]|"";
  if(!validate_manifest(selected["manifest"],entry,error)||
      std::string(selected["manifest"]["app"]["tier"]|"")!="sandbox_script") {
    if(error.empty())error="project_autorun_tier_invalid";
    return false;
  }
  source=selected["content"]|"";
  if(source.empty()){error="project_legacy_content_missing";return false;}
  program=std::string("project://")+id+"/"+entry;return true;
}
}
