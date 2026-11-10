#include "lunaredu_lunaros/kernel/service_registry.h"
#include "lunaredu_lunaros/device_api/standard_projections.h"
#include <ArduinoJson.h>

#include "lunaredu_lunaros/device_api/device_projection.h"

namespace lunaredu::lunaros {
namespace {

constexpr const char *kModuleId = "service";

void fill_status_payload(JsonObject payload) {
  const auto snapshot = service_registry_snapshot();
  payload["revision"] = snapshot.revision;
  payload["required_services_ready"] = snapshot.required_services_ready;
  JsonArray services = payload["services"].to<JsonArray>();
  for (size_t index = 0; index < snapshot.service_count; ++index) {
    const ServiceSnapshot &service = snapshot.services[index];
    JsonObject item = services.add<JsonObject>();
    item["id"] = service.id;
    item["title"] = service.title;
    item["owner_executor"] = service.owner_executor;
    // v1 wire compatibility only: no second owner or task dispatch path.
    item["owner_task"] = service.owner_executor;
    item["criticality"] = service_criticality_name(service.criticality);
    item["state"] = service_state_name(service.state);
    item["available"] = service.available;
    item["detail"] = service.detail;
    item["revision"] = service.revision;
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

void fill_capabilities_payload(JsonObject payload) {
  payload["contract_version"] = 1;
  payload["dependency_aware"] = true;
  payload["required_readiness"] = true;
  payload["health_states"] = true;
  payload["owner_executor_declared"] = true;
  payload["owner_task_declared"] = true;
  payload["fixed_service_executors"] = true;
}

void fill_contract(JsonObject contract) {
  contract["registry_version"] = 1;
  contract["single_owner_executor"] = true;
  contract["single_owner_task"] = true;
  contract["dependency_aware"] = true;
  contract["typed_command_registry"] = true;
}

const DeviceCapabilityDescriptor kServiceRegistryCapability = []() {
  DeviceCapabilityDescriptor descriptor;
  descriptor.id = "service_registry";
  return descriptor;
}();

constexpr DeviceOperationDefinition kServiceOperations[] = {
    device_operation_on_caller(make_device_json_provider("capabilities.get", fill_capabilities_payload)),
    device_operation_on_caller(make_device_json_provider("status.get", fill_status_payload)),
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

bool setup_service_projection(std::string &error) {
  return register_device_projection(
      {&kServiceRegistryCapability, &kServiceProjection, nullptr, true},
      &error);
}

} // namespace lunaredu::lunaros
