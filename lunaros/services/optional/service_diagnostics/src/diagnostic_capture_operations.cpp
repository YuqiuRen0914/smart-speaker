#include "diagnostic_capture_operations.h"
#include "diagnostics/log.h"
#include <cstdio>

namespace lunaredu::lunaros::internal {
namespace {
void decimal(JsonObject object, const char *name, uint64_t value) {
  char text[21];
  std::snprintf(text, sizeof(text), "%llu", static_cast<unsigned long long>(value));
  object[name] = text;
}
}
void diagnostic_capture_status(JsonObject response) {
  const auto status = debug_log_capture_status();
  response["contract_version"] = 1;
  response["enabled"] = status.enabled;
  response["generation"] = status.generation;
  response["allocated_bytes"] = status.allocated_bytes;
  response["record_count"] = status.record_count;
  response["expires"] = false;
  response["persistent"] = false;
  response["scope"] = "logs_and_operations";
}
DeviceOperationResult diagnostic_capture_set(JsonVariantConst request,
                                             JsonObject response) {
  if (!request.is<JsonObjectConst>() || request.as<JsonObjectConst>().size() != 1 ||
      !request["enabled"].is<bool>())
    return device_operation_error("diagnostic_capture_invalid_switch", 400, "expected_enabled_boolean");
  if (!set_debug_log_capture(request["enabled"].as<bool>()))
    return device_operation_error("diagnostic_capture_unavailable", 503, "capture_allocation_failed");
  diagnostic_capture_status(response);
  return device_operation_ack();
}
DeviceOperationResult diagnostic_capture_read(JsonVariantConst request,
                                              JsonObject response) {
  uint64_t after = 0;
  size_t limit = kDebugLogReadCapacity;
  if (!request.is<JsonObjectConst>())
    return device_operation_error("invalid_log_query", 400, "expected_object");
  for (JsonPairConst field : request.as<JsonObjectConst>()) {
    if (field.key() != "after" && field.key() != "limit" && field.key() != "from_oldest")
      return device_operation_error("invalid_log_query", 400, "unknown_field");
    if (field.value().isNull())
      return device_operation_error("invalid_log_query", 400, "null_field");
  }
  if (!request["after"].isNull()) {
    if (!request["after"].is<const char *>())
      return device_operation_error("invalid_log_cursor", 400, "expected_decimal_string");
    const char *text = request["after"];
    if (!*text) return device_operation_error("invalid_log_cursor", 400, "empty_cursor");
    for (; *text; ++text) {
      if (*text < '0' || *text > '9' || after > (UINT64_MAX - (*text - '0')) / 10)
        return device_operation_error("invalid_log_cursor", 400, "invalid_cursor");
      after = after * 10 + (*text - '0');
    }
  }
  if (!request["limit"].isNull()) {
    if (!request["limit"].is<unsigned>() || request["limit"].as<unsigned>() == 0 ||
        request["limit"].as<unsigned>() > kDebugLogReadCapacity)
      return device_operation_error("invalid_log_limit", 400, "limit_out_of_range");
    limit = request["limit"].as<unsigned>();
  }
  if (!request["from_oldest"].isNull() && !request["from_oldest"].is<bool>())
    return device_operation_error("invalid_log_query", 400, "expected_from_oldest_boolean");
  const bool from_oldest = request["from_oldest"] | false;
  if (from_oldest && after) return device_operation_error("invalid_log_query", 400, "conflicting_log_cursor");
  const auto logs = read_debug_logs(after, limit, from_oldest);
  response["enabled"] = logs.enabled;
  response["generation"] = logs.generation;
  decimal(response, "oldest_cursor", logs.oldest_cursor);
  decimal(response, "next_cursor", logs.next_cursor);
  response["cursor_reset"] = logs.cursor_reset;
  auto items = response["records"].to<JsonArray>();
  for (size_t i = 0; i < logs.count; ++i) {
    auto item = items.add<JsonObject>();
    decimal(item, "sequence", logs.records[i].sequence);
    decimal(item, "uptime_ms", logs.records[i].uptime_ms);
    const auto &record = logs.records[i];
    item["kind"] = record.trace_id ? "operation" : "log";
    if (record.trace_id) {
      decimal(item, "trace_id", record.trace_id);
      if (record.parent_trace_id) decimal(item, "parent_trace_id", record.parent_trace_id);
      if (record.request_sequence) {
        char request[32];
        format_device_request_id(request, sizeof(request), record.request_sequence);
        item["request_id"] = request;
      }
      item["phase"] = diagnostic_phase_name(record.phase);
      item["operation"] = record.message.data();
      if (record.phase == DiagnosticPhase::ActivityLinked)
        item["activity_id"] = record.message.data() + 80;
      else item["error_code"] = record.message.data() + 80;
      item["truncated"] = record.truncated;
      item["evidence"] = "software_only";
    } else item["message"] = record.message.data();
  }
  // Continue with the last returned sequence, not the buffer's next sequence.
  decimal(response, "after", logs.count ? logs.records[logs.count - 1].sequence
                                      : (logs.cursor_reset ? logs.next_cursor - 1 : after));
  return device_operation_ack();
}
} // namespace lunaredu::lunaros::internal
