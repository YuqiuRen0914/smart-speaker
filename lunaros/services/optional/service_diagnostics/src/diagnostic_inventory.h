#pragma once
#include "device_api/device_operation_builders.h"
namespace lunaredu::lunaros::internal {
void fill_diagnostic_resources(DeviceApiContext &, JsonObject);
void fill_diagnostic_activities(DeviceApiContext &, JsonObject, size_t offset = 0, size_t limit = 8);
DeviceOperationResult read_diagnostic_activities(DeviceApiContext &, JsonVariantConst, JsonObject);
DeviceOperationResult read_diagnostic_inventory(DeviceApiContext &, JsonVariantConst, JsonObject);
constexpr const char *kDiagnosticInventorySchema = R"({"type":"object","properties":{"offset":{"type":"integer","minimum":0,"maximum":4294967295},"limit":{"type":"integer","minimum":1,"maximum":16}},"additionalProperties":false})";
constexpr DeviceOperationDefinition diagnostic_inventory_operation() {
  auto operation = make_device_json_command("diagnostics.capabilities.get", read_diagnostic_inventory);
  operation.model.mutating = false;
  operation.model.kind = DeviceOperationKind::Query;
  return device_operation_schemas(operation, kDiagnosticInventorySchema, nullptr);
}
constexpr DeviceOperationDefinition diagnostic_activities_operation() {
  auto operation = make_device_json_command("diagnostics.activities.get", read_diagnostic_activities);
  operation.model.mutating = false;
  operation.model.kind = DeviceOperationKind::Query;
  return device_operation_schemas(operation,
      R"({"type":"object","properties":{"offset":{"type":"integer","minimum":0,"maximum":32},"limit":{"type":"integer","minimum":1,"maximum":12}},"additionalProperties":false})", nullptr);
}
}
