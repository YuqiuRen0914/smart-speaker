#include "lunaredu_lunaros/app/app_projection.h"
#include "lunaredu_lunaros/app/app_controller.h"

#include <ArduinoJson.h>

#include "lunaredu_lunaros/app/app_commands.h"
#include "lunaredu_lunaros/device_api/device_projection_json.h"
#include "lunaredu_lunaros/device_api/device_projection.h"

namespace lunaredu::app {
namespace {

using namespace lunaredu::lunaros;

constexpr const char *kModuleId = "app";

void fill_application(JsonArray applications,
                      const ApplicationSnapshot &application) {
  JsonObject item = applications.add<JsonObject>();
  item["id"] = application.id;
  item["title"] = application.title;
  item["icon"] = application.icon;
  item["launcher_accent_rgb"] = application.launcher_accent_rgb;
  item["version"] = application.version;
  item["kind"] = foreground_kind_name(application.kind);
  item["backend"] = application_backend_name(application.backend);
  item["visibility"] = application_visibility_name(application.visibility);
  item["system_ui"] =
      application_system_ui_policy_name(application.system_ui);
  item["available"] = application.available;
  item["unavailable_reason"] = application.unavailable_reason;
  JsonArray capabilities = item["capabilities"].to<JsonArray>();
  for (size_t capability_index = 0;
       capability_index < application.capability_count; ++capability_index) {
    const auto &capability = application.capabilities[capability_index];
    JsonObject capability_item = capabilities.add<JsonObject>();
    capability_item["id"] = capability.id;
    capability_item["required"] = capability.required;
    capability_item["available"] = capability.available;
  }
  JsonArray services = item["services"].to<JsonArray>();
  for (size_t service_index = 0;
       service_index < application.service_count; ++service_index) {
    const auto &service = application.services[service_index];
    JsonObject service_item = services.add<JsonObject>();
    service_item["id"] = service.id;
    service_item["required"] = service.required;
    service_item["available"] = service.available;
  }
  JsonArray resources = item["resources"].to<JsonArray>();
  for (size_t resource_index = 0;
       resource_index < application.resource_count; ++resource_index) {
    const auto &resource = application.resources[resource_index];
    JsonObject resource_item = resources.add<JsonObject>();
    resource_item["id"] = resource.id;
    resource_item["access"] =
        lunaredu::lunaros::resource_access_name(resource.access);
  }
  item["active"] = application.active;
  item["pending"] = application.pending;
}

void fill_status(JsonObject payload,
                 const AppControllerStatusSnapshot &snapshot) {
  payload["state"] = app_controller_state_name(snapshot.state);
  payload["active_application"] = snapshot.active_application_id;
  payload["pending_application"] = snapshot.pending_application_id;
  payload["previous_application"] = snapshot.previous_application_id;
  payload["transition_phase"] =
      app_transition_phase_name(snapshot.transition_phase);
  payload["transition_id"] = snapshot.transition_id;
  payload["last_error"] = snapshot.last_error;
}

uint16_t status_code_for_error(const std::string &error) {
  if (error == "application_not_found") {
    return 404;
  }
  if (error == "service_executor_unavailable" ||
      error == "service_executor_unbound" ||
      error == "application_controller_unavailable") {
    return 503;
  }
  return 409;
}

DeviceOperationResult dispatch_error(const std::string &error) {
  return device_operation_error(error.c_str(), status_code_for_error(error),
                                   "application_control_failed");
}

void fill_capabilities_payload(JsonObject payload) {
  payload["contract_version"] = 3;
  payload["single_foreground_application"] = true;
  payload["supports_native"] = true;
  payload["supports_micropython"] = true;
  payload["supports_manifests"] = true;
  payload["supports_capability_requirements"] = true;
  payload["supports_service_requirements"] = true;
  payload["supports_resource_leases"] = true;
  payload["switch_rolls_back_on_start_failure"] = true;
  payload["switch_preflight_preserves_foreground"] = true;
  payload["switch_is_async"] = true;

  JsonArray operations = payload["supported_ops"].to<JsonArray>();
  operations.add("list");
  operations.add("status");
  operations.add("switch");
  operations.add("stop");
}

DeviceOperationResult fill_status_payload(JsonObject payload) {
  AppControllerStatusSnapshot snapshot;
  std::string error;
  if (!request_application_status(snapshot, error)) {
    return dispatch_error(error);
  }
  fill_status(payload, snapshot);
  return device_operation_ack();
}

DeviceOperationResult fill_list_payload(JsonObject payload) {
  AppControllerStatusSnapshot snapshot;
  std::string error;
  if (!request_application_status(snapshot, error)) {
    return dispatch_error(error);
  }
  fill_status(payload, snapshot);
  JsonArray applications = payload["applications"].to<JsonArray>();
  ApplicationSnapshot application;
  for (size_t index = 0; index < snapshot.application_count; ++index) {
    if (!request_application_snapshot(index, application, error)) {
      return dispatch_error(error);
    }
    fill_application(applications, application);
  }
  return device_operation_ack();
}

DeviceOperationResult handle_switch_payload(JsonVariantConst request,
                                               JsonObject response) {
  const std::string application_id =
      device_operation_string(request["application_id"]);
  if (application_id.empty()) {
    return device_operation_error("missing_application_id", 400,
                                     "application_control_failed");
  }

  AppControllerStatusSnapshot snapshot;
  std::string error;
  if (!request_application_switch(application_id, snapshot, error)) {
    fill_status(response, snapshot);
    return dispatch_error(error);
  }
  fill_status(response, snapshot);
  return device_operation_ack(202);
}

DeviceOperationResult handle_stop_payload(JsonVariantConst request,
                                             JsonObject response) {
  std::string reason = device_operation_string(request["reason"]);
  if (reason.empty()) {
    reason = "remote_stop";
  }

  AppControllerStatusSnapshot snapshot;
  std::string error;
  if (!request_application_stop(reason, snapshot, error)) {
    return dispatch_error(error);
  }
  fill_status(response, snapshot);
  return device_operation_ack();
}

void fill_contract(JsonObject contract) {
  contract["manifest_version"] = 1;
  contract["single_foreground_application"] = true;
  contract["switch_is_async"] = true;
  contract["capability_driven"] = true;
  contract["service_dependencies"] = true;
  contract["resource_leases"] = true;
  contract["transactional_rollback"] = true;
}

const DeviceCapabilityDescriptor kApplicationCapability = []() {
  DeviceCapabilityDescriptor descriptor;
  descriptor.id = "application_controller";
  return descriptor;
}();

constexpr DeviceOperationDefinition kApplicationOperations[] = {
    make_device_json_provider("capabilities.get", fill_capabilities_payload),
    make_device_json_query("list", fill_list_payload),
    make_device_json_query("status.get", fill_status_payload),
    make_device_json_command("switch", handle_switch_payload),
    make_device_json_command("stop", handle_stop_payload),
};

const DeviceModuleProjectionMetadata kApplicationModule = {
    kModuleId,
    "application_controller",
    fill_contract,
};

const DeviceModuleProjection kApplicationProjection = {
    "application_controller",
    &kApplicationModule,
    kApplicationOperations,
    sizeof(kApplicationOperations) / sizeof(kApplicationOperations[0]),
};

} // namespace

bool setup_app_projection(std::string &error) {
  if (!register_device_projection(
          {&kApplicationCapability, &kApplicationProjection, nullptr, false},
          &error)) {
    return false;
  }
  (void)deactivate_device_capability(kApplicationCapability.id);
  return true;
}

bool set_app_projection_available(bool available, std::string &error) {
  error.clear();
  if (!available) {
    (void)deactivate_device_capability(kApplicationCapability.id);
    return true;
  }
  return activate_device_capability(kApplicationCapability, &error);
}

} // namespace lunaredu::app
