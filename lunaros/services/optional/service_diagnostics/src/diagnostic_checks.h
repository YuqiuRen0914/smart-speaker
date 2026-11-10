#pragma once
#include "device_api/device_operation_builders.h"
namespace lunaredu::lunaros::internal {
void fill_diagnostic_checks(DeviceApiContext &, JsonObject);
DeviceOperationResult run_diagnostic_check(DeviceApiContext &, JsonVariantConst, JsonObject);
constexpr DeviceOperationDefinition diagnostic_check_operation() {
  auto op = make_device_json_command("diagnostics.checks.run", run_diagnostic_check);
  op.model.mutating = false;
  op.model.kind = DeviceOperationKind::Query;
  return device_operation_schemas(op,
      R"({"type":"object","required":["check"],"properties":{"check":{"enum":["operation.preflight","memory.summary","resource.owners","activity.terminal"]},"module":{"type":"string","maxLength":128},"operation":{"type":"string","maxLength":128},"activity_id":{"type":"string","maxLength":31},"minimum_contiguous_bytes":{"type":"integer","minimum":0}},"additionalProperties":false})", nullptr);
}
}
