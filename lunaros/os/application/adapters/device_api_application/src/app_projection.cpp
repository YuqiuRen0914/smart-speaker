#include "device_api/adapters/application_projection.h"
#include "device_api/core_projections.h"
#include "app/lifecycle/snapshot_names.h"
#include "app/execution/backend_catalog.h"
#include "device_api/device_operation_builders.h"
#include "model/device_model.h"
#include "device_api/device_api_context.h"
#include "app/lifecycle/app_controller.h"
#include "app/execution/application_backend.h"
#include "app/execution/application_device.h"
#include "app/package/application_profile.h"

#include <ArduinoJson.h>

#include "app/lifecycle/app_commands.h"
#include "device_api/device_projection.h"

namespace lunaredu::lunaros {
// Only this adapter implementation can recover the borrowed application view.
class ApplicationProtocolAccess {
public:
  static ApplicationContext *context(const DeviceApiContext &device) {
    const auto *binding = ApplicationProtocolBinding::bound(device);
    return binding ? &binding->application_ : nullptr;
  }
};
bool device_api_switch_application(DeviceApiContext &device, const std::string &id,
    app::AppControllerStatusSnapshot &snapshot, std::string &error) {
  auto *application = ApplicationProtocolAccess::context(device);
  if (!application) { snapshot={};error="application_controller_unavailable";return false; }
  return app::request_application_switch(*application,id,snapshot,error);
}
}

namespace lunaredu::app {
namespace {

const char *application_preparation_name(ApplicationPreparationState state) {
  switch (state) {
    case ApplicationPreparationState::Unprepared: return "unprepared";
    case ApplicationPreparationState::Preparing: return "preparing";
    case ApplicationPreparationState::Ready: return "ready";
    case ApplicationPreparationState::Failed: return "failed";
  }
  return "unprepared";
}


using namespace lunaredu::lunaros;

constexpr const char *foreground_kind_name(ForegroundKind kind) {
  switch (kind) {
  case ForegroundKind::Application:
    return "application";
  case ForegroundKind::SystemSurface:
    return "system_surface";
  case ForegroundKind::Mode:
    return "mode";
  }
  return "application";
}

constexpr const char *application_backend_name(ApplicationBackend backend) {
  switch (backend) {
  case ApplicationBackend::Native:
    return "native";
  case ApplicationBackend::MicroPython:
    return "micropython";
  }
  return "unknown";
}

constexpr const char *application_visibility_name(ForegroundKind kind) {
  switch (kind) {
  case ForegroundKind::Application:
    return "launcher";
  case ForegroundKind::Mode:
    return "hidden";
  case ForegroundKind::SystemSurface:
    return "system";
  }
  return "hidden";
}

constexpr const char *application_system_ui_policy_name(
    ApplicationSystemUiPolicy policy) {
  switch (policy) {
  case ApplicationSystemUiPolicy::Standard:
    return "standard";
  case ApplicationSystemUiPolicy::Immersive:
    return "immersive";
  }
  return "standard";
}

constexpr const char *resource_access_name(ResourceAccess access) {
  switch (access) {
  case ResourceAccess::Shared:
    return "shared";
  case ResourceAccess::Exclusive:
    return "exclusive";
  }
  return "exclusive";
}

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
  item["visibility"] = application_visibility_name(application.kind);
  item["system_ui"] =
      application_system_ui_policy_name(application.system_ui);
  item["is_home"] = application.is_home;
  item["available"] = application.available;
  item["unavailable_reason"] = application.unavailable_reason;
  if (application.has_preparation) {
    auto preparation = item["preparation"].to<JsonObject>();
    preparation["state"] = application_preparation_name(application.preparation.state);
    preparation["error"] = application.preparation.error;
    preparation["retryable"] = application.preparation.state == ApplicationPreparationState::Failed;
  }
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
        resource_access_name(resource.access);
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

void fill_capabilities_payload([[maybe_unused]] DeviceApiContext &device, JsonObject payload) {
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
  // Keep native app-controller capabilities separate from downloadable guest
  // execution. The same projection is available through HTTP and USB.
  const auto execution = apps::application_execution_capabilities();
  auto downloadable = payload["downloadable_execution"].to<JsonObject>();
  downloadable["schema"] = "lunaros.execution-capabilities/v1";
  downloadable["wasm"] = execution.wasm;
  downloadable["micropython"] = execution.micropython;
  downloadable["abi"] = execution.abi;
  downloadable["memory_pages"] = execution.memory_pages;
  downloadable["maximum_package_bytes"] = execution.maximum_package_bytes;
  auto hardware=downloadable["device_basic"].to<JsonObject>();
  hardware["permission"]="device.basic";
  hardware["contract_version"]=LUNA_DEVICE_CONTRACT_VERSION;
  hardware["describes_contract_only"]=true;
  hardware["availability"]="guest_query_capabilities";
  const auto *wasm_contract=apps::application_backend_contract("wasm");
  hardware["wasm_permission_supported"]=execution.wasm&&wasm_contract&&wasm_contract->supports_device_basic;
  hardware["callback_budget"]=LUNA_DEVICE_CALLBACK_BUDGET;
  hardware["cache_ms"]=LUNA_DEVICE_CACHE_MS;
  hardware["write_acknowledgement"]=operation_completion_name(apps::kApplicationDeviceWriteAcknowledgement);
  hardware["read_acknowledgement"]=operation_completion_name(apps::kApplicationDeviceReadAcknowledgement);
  auto contracts=hardware["operations"].to<JsonArray>();
  for(const auto &contract:apps::application_device_contracts()) {
    auto item=contracts.add<JsonObject>();
    item["operation"]=contract.operation;item["id"]=contract.id;item["unit"]=contract.unit;
    item["minimum"]=contract.minimum;item["maximum"]=contract.maximum;
    item["writes_output"]=contract.writes_output;item["read_channels"]=contract.read_channels;
  }
  downloadable["screen"]["width"] = apps::kApplicationPlatform.canvas_width;
  downloadable["screen"]["height"] = apps::kApplicationPlatform.canvas_height;
  auto packages = downloadable["package_versions"].to<JsonArray>();
  packages.add(apps::kApplicationPlatform.package_version);
  auto targets = downloadable["aot_targets"].to<JsonArray>();
  const auto *aot_profile=apps::application_artifact_profile("aot");
  if (execution.aot&&aot_profile) {
    auto target = targets.add<JsonObject>();
    target["target"]=aot_profile->target; target["cpu"]=aot_profile->cpu;
    target["runtime_revision"]=aot_profile->runtime_revision;
    target["abi"]=aot_profile->abi; target["aot_format"]=aot_profile->format;
    target["protection_profile"]=aot_profile->protection_profile;
  }

  JsonArray operations = payload["supported_ops"].to<JsonArray>();
  operations.add("list");
  operations.add("status");
  operations.add("switch");
  operations.add("stop");
}

DeviceOperationResult fill_status_payload([[maybe_unused]] DeviceApiContext &device, JsonObject payload) {
  if (!ApplicationProtocolAccess::context(device)) return device_operation_error("application_controller_unavailable", 503);
  AppControllerStatusSnapshot snapshot;
  std::string error;
  if (!request_application_status(*ApplicationProtocolAccess::context(device), snapshot, error)) {
    return dispatch_error(error);
  }
  fill_status(payload, snapshot);
  return device_operation_ack();
}

DeviceOperationResult fill_list_payload([[maybe_unused]] DeviceApiContext &device, JsonObject payload) {
  if (!ApplicationProtocolAccess::context(device)) return device_operation_error("application_controller_unavailable", 503);
  AppControllerStatusSnapshot snapshot;
  std::string error;
  if (!request_application_status(*ApplicationProtocolAccess::context(device), snapshot, error)) {
    return dispatch_error(error);
  }
  fill_status(payload, snapshot);
  JsonArray applications = payload["applications"].to<JsonArray>();
  ApplicationSnapshot application;
  for (size_t index = 0; index < snapshot.application_count; ++index) {
    if (!request_application_snapshot(*ApplicationProtocolAccess::context(device), index, application, error)) {
      return dispatch_error(error);
    }
    fill_application(applications, application);
  }
  return device_operation_ack();
}

DeviceOperationResult handle_switch_payload([[maybe_unused]] DeviceApiContext &device, JsonVariantConst request,
                                               JsonObject response) {
  if (!ApplicationProtocolAccess::context(device)) return device_operation_error("application_controller_unavailable", 503);
  const std::string application_id =
      device_operation_string(request["application_id"]);
  if (application_id.empty()) {
    return device_operation_error("missing_application_id", 400,
                                     "application_control_failed");
  }

  AppControllerStatusSnapshot snapshot;
  std::string error;
  if (!device_api_switch_application(device, application_id, snapshot, error)) {
    fill_status(response, snapshot);
    return dispatch_error(error);
  }
  fill_status(response, snapshot);
  return device_operation_ack(202,::lunaredu::lunaros::OperationCompletion::Accepted);
}

DeviceOperationResult handle_stop_payload([[maybe_unused]] DeviceApiContext &device, JsonVariantConst request,
                                             JsonObject response) {
  if (!ApplicationProtocolAccess::context(device)) return device_operation_error("application_controller_unavailable", 503);
  std::string reason = device_operation_string(request["reason"]);
  if (reason.empty()) {
    reason = "remote_stop";
  }

  AppControllerStatusSnapshot snapshot;
  std::string error;
  if (!request_application_stop(*ApplicationProtocolAccess::context(device), reason, snapshot, error)) {
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


constexpr DeviceOperationDefinition kApplicationOperations[] = {
    device_operation_remote(make_device_json_provider("capabilities.get", fill_capabilities_payload), DeviceRemoteAccess::Query),
    device_operation_remote(make_device_json_query("list", fill_list_payload), DeviceRemoteAccess::Query),
    device_operation_remote(make_device_json_query("status.get", fill_status_payload), DeviceRemoteAccess::Query),
    device_operation_remote(make_device_json_command("switch", handle_switch_payload), DeviceRemoteAccess::Command),
    device_operation_remote(make_device_json_command("stop", handle_stop_payload), DeviceRemoteAccess::Command),
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

bool install_app_projection(DeviceProjectionRegistry &registry, std::string &error) {
  // Declare transport contracts during boot composition, before a native
  // product finishes registering its foreground catalog. AppController::setup
  // activates this capability; commands remain unavailable until then.
  if (!registry.register_projection(
          {&kApplicationCapability, &kApplicationProjection, nullptr, false},
          &error)) {
    return false;
  }
  (void)registry.context().capabilities.deactivate(kApplicationCapability.id);
  return true;
}

} // namespace
} // namespace lunaredu::app

namespace lunaredu::lunaros {
const StandardProjectionContribution standard_projection::applications{
    "applications", app::install_app_projection};
}
