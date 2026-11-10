#include "device_api/device_operation_builders.h"
#include "model/device_model.h"
#include "device_api/device_api_context.h"
#include "device_api/projection_composition.h"

#include <cstdlib>

#include <ArduinoJson.h>
#include "sdkconfig.h"

#include "device_api/device_projection.h"
#include "diagnostics/session.h"
#include "diagnostics/log.h"

namespace lunaredu::lunaros {
#ifndef CONFIG_LUNAREDU_LOCAL_DEBUG_SURFACE
#define CONFIG_LUNAREDU_LOCAL_DEBUG_SURFACE 0
#endif
#if CONFIG_LUNAREDU_LOCAL_DEBUG_SURFACE
namespace {

constexpr const char *kModuleId = "debug";
void fill_capabilities([[maybe_unused]] DeviceApiContext &device, JsonObject payload) {
  const bool screen_available = device.model.capabilities.active("screen_canvas");
  payload["contract_version"] = 2;
  payload["bounded_logs"] = true;
  payload["capture_switch"] = true;
  payload["capture_default_enabled"] = false;
  payload["cursor_logs"] = true;
  payload["diagnostic_snapshot"] = true;
  payload["feedback_export"] = "client_side";
  payload["screen_capture"] = screen_available;
  payload["input_sequence"] = screen_available;
  payload["session_required"] = true;
}

DeviceOperationResult read_logs([[maybe_unused]] DeviceApiContext &device, const DeviceBulkRequest &request,
                                JsonObject response) {
  uint64_t after = 0;
  size_t limit = kDebugLogReadCapacity;
  if (request.has_arg != nullptr && request.arg != nullptr &&
      request.has_arg("after", request.context)) {
    const std::string value = request.arg("after", request.context);
    char *end = nullptr;
    after = std::strtoull(value.c_str(), &end, 10);
    if (end == value.c_str() || *end != '\0') {
      return device_operation_error("invalid_log_cursor", 400,
                                    "debug_log_read_failed");
    }
  }
  if (request.has_arg != nullptr && request.arg != nullptr &&
      request.has_arg("limit", request.context)) {
    const std::string value = request.arg("limit", request.context);
    char *end = nullptr;
    const unsigned long parsed = std::strtoul(value.c_str(), &end, 10);
    if (end == value.c_str() || *end != '\0' || parsed == 0) {
      return device_operation_error("invalid_log_limit", 400,
                                    "debug_log_read_failed");
    }
    limit = static_cast<size_t>(parsed);
  }

  const auto result = read_debug_logs(after, limit);
  response["enabled"] = result.enabled;
  response["generation"] = result.generation;
  response["oldest_cursor"] = result.oldest_cursor;
  response["next_cursor"] = result.next_cursor;
  response["cursor_reset"] = result.cursor_reset;
  JsonArray records = response["records"].to<JsonArray>();
  for (size_t index = 0; index < result.count; ++index) {
    const auto &record = result.records[index];
    JsonObject item = records.add<JsonObject>();
    item["sequence"] = record.sequence;
    item["uptime_ms"] = record.uptime_ms;
    item["source"] = record.trace_id ? "device-operation" : "esp-idf";
    item["message"] = record.message.data();
    if (record.trace_id) {
      item["trace_id"] = record.trace_id;
      if (record.parent_trace_id) item["parent_trace_id"] = record.parent_trace_id;
      item["phase"] = diagnostic_phase_name(record.phase);
      if (record.request_sequence) {
        char request_id[32];
        format_device_request_id(request_id, sizeof(request_id), record.request_sequence);
        item["request_id"] = request_id;
      }
      if (record.phase == DiagnosticPhase::ActivityLinked)
        item["activity_id"] = record.message.data() + 80;
      else item["error_code"] = record.message.data() + 80;
    }
  }
  return device_operation_ack();
}

void fill_contract(JsonObject contract) {
  contract["version"] = 1;
  contract["bounded_memory"] = true;
  contract["cursor_is_opaque"] = true;
  contract["transport_neutral"] = true;
}

const DeviceCapabilityDescriptor kDebugCapability{"runtime_debug"};

const DeviceOperationDefinition kDebugOperations[] = {
    device_operation_diagnostic_access(device_operation_on_caller(
        make_device_json_provider("capabilities.get", fill_capabilities)),
        DeviceDiagnosticAccess::Bootstrap),
    device_operation_diagnostic_access(
        device_operation_on_caller(make_device_bulk_read("logs.read", read_logs)),
        DeviceDiagnosticAccess::Session),
};

const DeviceModuleProjectionMetadata kDebugModule = {
    kModuleId,
    "runtime_debug",
    fill_contract,
};

const DeviceModuleProjection kDebugProjection = {
    "runtime_debug",
    &kDebugModule,
    kDebugOperations,
    std::size(kDebugOperations),
};

} // namespace
#endif

bool install_diagnostics_debug_projection(DeviceProjectionRegistry &registry, std::string &error) {
#if CONFIG_LUNAREDU_LOCAL_DEBUG_SURFACE
  install_debug_log_collector();
  initialize_diagnostic_session();
  return registry.register_projection(
      {&kDebugCapability, &kDebugProjection, nullptr, true}, &error);
#else
  (void)registry;
  error.clear();
  return true;
#endif
}



} // namespace lunaredu::lunaros
