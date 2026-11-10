#pragma once
#include "device_api/device_operation_builders.h"

namespace lunaredu::lunaros::internal {
inline constexpr const char *kDiagnosticCaptureSetSchema =
    R"({"type":"object","properties":{"enabled":{"type":"boolean"}},"required":["enabled"],"additionalProperties":false})";
void diagnostic_capture_status(JsonObject);
DeviceOperationResult diagnostic_capture_set(JsonVariantConst, JsonObject);
DeviceOperationResult diagnostic_capture_read(JsonVariantConst, JsonObject);
inline void fill_diagnostic_capture_status(DeviceApiContext &, JsonObject response) {
  diagnostic_capture_status(response);
}
inline DeviceOperationResult set_diagnostic_capture(DeviceApiContext &, JsonVariantConst request, JsonObject response) {
  return diagnostic_capture_set(request, response);
}
inline constexpr DeviceJsonBinding kDiagnosticCaptureReadBinding{
    [](void *, JsonVariantConst request, JsonObject response) {
      return diagnostic_capture_read(request, response);
    }, nullptr};
// POST carries pagination JSON; the Device Model operation remains read-only.
// The current USB transport only supports parameterless GET queries.
constexpr DeviceOperationDefinition diagnostic_capture_read_operation() {
  auto operation = make_device_json_binding("diagnostics.capture.read", kDiagnosticCaptureReadBinding, false);
  operation.projection.method = DeviceOperationMethod::Post;
  operation.projection.handler_kind = DeviceOperationHandlerKind::JsonCommand;
  return device_operation_schemas(operation,
      R"({"type":"object","properties":{"after":{"type":"string","pattern":"^[0-9]+$"},"limit":{"type":"integer","minimum":1,"maximum":12},"from_oldest":{"type":"boolean"}},"additionalProperties":false})", nullptr);
}
} // namespace lunaredu::lunaros::internal
