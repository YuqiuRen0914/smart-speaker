#include "device_api/core_projections.h"
#include "device_api/device_operation_builders.h"
#include "kernel/runtime.h"
#include "model/device_model.h"
#include "device_api/device_api_context.h"
#include "kernel/service.h"
#include <ArduinoJson.h>

#include "device_api/device_projection.h"

namespace lunaredu::lunaros {
namespace {

constexpr const char *kModuleId = "service";

void fill_status_payload([[maybe_unused]] DeviceApiContext &device, JsonObject payload) {
  const auto snapshot = device.runtime.service_snapshot();
  payload["revision"] = snapshot.revision;
  payload["required_services_ready"] = snapshot.required_services_ready;
  JsonObject storage = payload["storage"].to<JsonObject>();
  storage["memory"] = snapshot.storage.memory;
  storage["capacity"] = snapshot.storage.capacity;
  storage["storage_bytes"] = snapshot.storage.storage_bytes;
  JsonArray services = payload["services"].to<JsonArray>();
  for (size_t index = 0; index < snapshot.service_count; ++index) {
    const ServiceSnapshot &service = snapshot.services[index];
    JsonObject item = services.add<JsonObject>();
    item["id"] = service.id;
    item["title"] = service.title;
    item["owner_executor"] = service.owner_executor;
    item["criticality"] = service_criticality_name(service.criticality);
    item["state"] = service_state_name(service.state);
    item["available"] = service.available;
    item["restartable"] = service.criticality == ServiceCriticality::Optional;
    item["detail"] = service.detail;
    item["revision"] = service.revision;
    item["explicit_uses"] = service.explicit_uses;
    item["on_demand"] = service.on_demand;
    item["has_admission_policy"] = service.has_admission_policy;
    JsonArray dependencies = item["dependencies"].to<JsonArray>();
    for (size_t dependency_index = 0;
         dependency_index < service.dependency_count; ++dependency_index) {
      const auto &dependency = service.dependencies[dependency_index];
      JsonObject dependency_item = dependencies.add<JsonObject>();
      dependency_item["id"] = dependency.id;
      dependency_item["required"] = dependency.required;
      dependency_item["available"] = dependency.available;
    }
  }
}

void fill_capabilities_payload([[maybe_unused]] DeviceApiContext &device, JsonObject payload) {
  payload["contract_version"] = 1;
  payload["dependency_aware"] = true;
  payload["required_readiness"] = true;
  payload["health_states"] = true;
  payload["owner_executor_declared"] = true;
  payload["fixed_service_executors"] = true;
}

void fill_contract(JsonObject contract) {
  contract["registry_version"] = 1;
  contract["single_owner_executor"] = true;
  contract["dependency_aware"] = true;
  contract["typed_command_registry"] = true;
}

DeviceOperationResult restart_service(DeviceApiContext &device,
    const DeviceBulkRequest &request, JsonObject payload) {
  // Authority comes from the transport, never from an input field. An ordinary
  // bound-device grant or serial diagnostic session cannot restart services.
  if (!request.administrator_authorized)
    return {false, 403, "administrator_required",OperationCompletion::NotStarted,OperationError::Forbidden};
  const char *id = request.payload["id"] | "";
  const auto snapshot = device.runtime.service_snapshot();
  for (size_t index = 0; index < snapshot.service_count; ++index) {
    const auto &service = snapshot.services[index];
    if (std::string(service.id) != id) continue;
    if (service.criticality != ServiceCriticality::Optional)
      return {false, 409, "required_service_restart_denied",OperationCompletion::NotStarted,OperationError::Forbidden};
    std::string error;
    if (!device.runtime.restart_service(id, error)) return {false, 409, error,OperationCompletion::Unknown,OperationError::Unavailable};
    payload["id"] = id;
    payload["available"] = device.runtime.service_available(id);
    return {};
  }
  return {false, 404, "service_not_found",OperationCompletion::NotStarted,OperationError::NotFound};
}

const DeviceCapabilityDescriptor kServiceRegistryCapability = []() {
  DeviceCapabilityDescriptor descriptor;
  descriptor.id = "service_registry";
  return descriptor;
}();

constexpr DeviceOperationDefinition kServiceOperations[] = {
    device_operation_on_caller(make_device_json_provider("capabilities.get", fill_capabilities_payload)),
    device_operation_on_caller(make_device_json_provider("status.get", fill_status_payload)),
    device_operation_diagnostic_access(make_device_bulk_write("restart", restart_service,
        device_operation_method(DeviceOperationMethod::Post)), DeviceDiagnosticAccess::Session),
};

const DeviceModuleProjectionMetadata kServiceModule = {
    kModuleId,
    "service_registry",
    fill_contract,
};

const DeviceModuleProjection kServiceProjection = {
    "service_registry",
    &kServiceModule,
    kServiceOperations,
    sizeof(kServiceOperations) / sizeof(kServiceOperations[0]),
};

} // namespace

static bool install_services_projection(DeviceProjectionRegistry &registry, std::string &error) {
  return registry.register_projection(
      {&kServiceRegistryCapability, &kServiceProjection, nullptr, true},
      &error);
}

const StandardProjectionContribution standard_projection::services{"service", install_services_projection};

} // namespace lunaredu::lunaros
