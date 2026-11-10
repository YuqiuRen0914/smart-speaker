#include "official_tools_operations.h"
#include "official_tools.h"
#include "trace_retention.h"
#include "diagnostic_outbox.h"
#include "insights_tools.h"
#include "diagnostics/session.h"
#include "session.h"
#include <cstring>
namespace lunaredu::lunaros::internal {
void fill_official_tools_status(JsonObject payload) {
  const auto facts=official_tools::status();
  payload["contract_version"]=1;
  payload["trace_mode"]=facts.trace_mode;
  payload["core_dump"]=facts.core_dump;
  payload["available"]=facts.available;
  payload["active"]=facts.active;
  payload["retiring"]=facts.retiring;
  payload["remaining_ms"]=facts.remaining_ms;
  payload["generation"]=facts.generation;
  payload["host_connected"]=facts.host_connected;
  payload["submitted_records"]=facts.emitted;
  payload["dropped_records"]=facts.dropped;
  payload["last_sdk_error"]=facts.last_error;
  payload["operation_capture_enabled"]=diagnostic_capture_enabled();
  payload["persistent"]=false;
  payload["insights_metric_set"]=insights_tools::metric_profile();
  const auto retention=trace_retention::status();
  auto trace=payload["trace_retention"].to<JsonObject>();
  trace["configured"]=retention.configured;trace["capturing"]=retention.capturing;
  trace["pending"]=retention.pending;trace["bytes"]=retention.bytes;
  trace["capacity"]=retention.capacity;trace["truncated"]=retention.truncated;
  trace["staging_capacity"]=retention.staging_capacity;trace["staged_bytes"]=retention.staged_bytes;
  trace["dropped_chunks"]=retention.dropped_chunks;trace["last_result"]=retention.last_result;
  payload["sdk_boot_lifetime"]=std::strcmp(facts.trace_mode,"none")!=0 || std::strcmp(facts.core_dump,"none")!=0;
  payload["core_dump_full_external_ram_included"]=false;
  const auto upload=diagnostic_outbox::status();
  auto field=payload["field_upload"].to<JsonObject>();
  field["configured"]=upload.configured;field["accepting"]=upload.accepting;
  field["io_active"]=upload.io_active;field["inventory_known"]=upload.inventory_known;
  if(upload.inventory_known)field["retained_records"]=upload.retained_records;
  else field["retained_records"]=nullptr;
  field["last_attempt_ms"]=upload.last_attempt_ms;field["last_result"]=upload.last_result;
}
DeviceOperationResult start_official_tools(JsonVariantConst request, JsonObject response) {
  if (!request.is<JsonObjectConst>()) return device_operation_error("official_trace_invalid_request",400,"expected_object");
  uint32_t duration=30'000;
  for (auto field:request.as<JsonObjectConst>()) {
    if (field.key() != "duration_ms") return device_operation_error("official_trace_invalid_request",400,"unknown_field");
    if (!field.value().is<uint32_t>()) return device_operation_error("official_trace_invalid_duration",400,"expected_integer");
    duration=field.value().as<uint32_t>();
  }
  if (duration < 100 || duration > 900'000) return device_operation_error("official_trace_invalid_duration",400,"duration_out_of_range");
  std::string error;
  if (!diagnostics::start_official_trace(duration,error))
    return device_operation_error(error.c_str(),error=="debug_session_required"?403:error=="debug_session_expiring"?409:503,"official_trace_start_failed");
  fill_official_tools_status(response);
  return device_operation_ack();
}
DeviceOperationResult stop_official_tools(JsonVariantConst, JsonObject response) {
  std::string error;
  if (!official_tools::stop(error)) return device_operation_error(error.c_str(),503,"official_trace_stop_failed");
  fill_official_tools_status(response);
  return device_operation_ack();
}
}
