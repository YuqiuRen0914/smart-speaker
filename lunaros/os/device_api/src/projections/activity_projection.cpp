#include "device_api/core_projections.h"
#include "device_api/device_operation_builders.h"
#include <cstdio>

#include <ArduinoJson.h>

#include "device_api/device_projection.h"
#include "device_api/device_api_context.h"
#include "model/device_activity.h"
#include "system/identity.h"

namespace lunaredu::lunaros {
namespace {

void fill_activity(JsonObject item, const DeviceActivitySnapshot &activity) {
  item["schema_version"] = "1.0.0";
  item["kind"] = "lunarlink.activity";
  item["activity_id"] = activity.activity_id;
  item["request_id"] = activity.request_id;
  if (activity.trace_id) {
    char trace[21];
    std::snprintf(trace, sizeof(trace), "%llu", static_cast<unsigned long long>(activity.trace_id));
    item["trace_id"] = trace;
  } else item["trace_id"] = nullptr;
  item["device_id"] = hardware_serial_number();
  item["capability_id"] = activity.capability_id;
  item["operation_id"] = activity.operation_id;
  item["state"] = device_activity_state_name(activity.state);
  item["accepted_at"] = activity.accepted_at;
  if (!activity.started_at.empty()) item["started_at"] = activity.started_at;
  item["updated_at"] = activity.updated_at;
  if (!activity.finished_at.empty()) item["finished_at"] = activity.finished_at;
  JsonObject progress = item["progress"].to<JsonObject>();
  progress["kind"] = "determinate";
  progress["value"] = activity.progress_percent;
  progress["unit"] = "percent";
  progress["message"] = activity.progress_message;
  if (!activity.result_json.empty()) {
    JsonDocument result;
    if (deserializeJson(result, activity.result_json) == DeserializationError::Ok)
      item["result"].set(result.as<JsonVariantConst>());
  }
  if (!activity.error_code.empty()) {
    JsonObject error = item["error"].to<JsonObject>();
    error["code"] = activity.error_code;
    error["message"] = activity.error_message;
    error["retryable"] = false;
    error["safe_recovery"].to<JsonArray>();
  }
  item["side_effects"].to<JsonArray>();
  item["cancelable"] = activity.cancelable &&
                         !device_activity_terminal(activity.state);
  item["revision"] = activity.revision;
  item["details_truncated"] = activity.details_truncated;
  item["result_omitted"] = activity.result_omitted;
}

DeviceOperationResult fill_activity_list(DeviceApiContext &device,
                                         JsonObject response) {
  response["ok"] = true;
  response["revision"] = device.model.activities.revision();
  JsonArray activities = response["activities"].to<JsonArray>();
  struct Context { JsonArray *activities; } context{&activities};
  device.model.activities.visit(
      [](const DeviceActivitySnapshot &activity, void *opaque) {
        auto &target = *static_cast<Context *>(opaque);
        fill_activity(target.activities->add<JsonObject>(), activity);
      },
      &context);
  response["count"] = activities.size();
  return device_operation_ack();
}

DeviceOperationResult handle_activity_get(DeviceApiContext &device,
                                          JsonVariantConst request,
                                          JsonObject response) {
  const std::string activity_id = request["activity_id"] | "";
  DeviceActivitySnapshot snapshot;
  if (!device.model.activities.get(activity_id.c_str(), snapshot))
    return device_operation_error("activity_not_found", 404,
                                  "activity_lookup_failed",OperationCompletion::NotStarted,OperationError::NotFound);
  fill_activity(response, snapshot);
  return device_operation_ack();
}

DeviceOperationResult handle_activity_cancel(DeviceApiContext &device,
                                             JsonVariantConst request,
                                             JsonObject response) {
  const std::string activity_id = request["activity_id"] | "";
  DeviceActivitySnapshot snapshot;
  if (!device.model.activities.get(activity_id.c_str(), snapshot))
    return device_operation_error("activity_not_found", 404,
                                  "activity_cancel_failed",OperationCompletion::NotStarted,OperationError::NotFound);
  if (!device.model.activities.cancel(activity_id.c_str(), "canceled by user"))
    return device_operation_error("activity_not_cancelable", 409,
                                  "activity_cancel_failed",OperationCompletion::NotStarted,OperationError::Conflict);
  (void)device.model.activities.get(activity_id.c_str(), snapshot);
  fill_activity(response, snapshot);
  return device_operation_ack();
}

void fill_contract(JsonObject contract) {
  contract["schema"] = "lunarlink.activity/1.0.0";
  contract["history_capacity"] = kDeviceActivityCapacity;
  contract["protects_active_history"] = true;
  contract["message_max_bytes"] = kDeviceActivityMessageBytes;
  contract["result_max_bytes"] = kDeviceActivityResultBytes;
  contract["terminal_feedback"] = true;
  contract["cancel_supported"] = true;
}

constexpr const char *kActivityIdRequest =
    R"json({"type":"object","additionalProperties":false,"required":["activity_id"],"properties":{"activity_id":{"type":"string","pattern":"^act_[a-z0-9-]{8,64}$"}}})json";

constexpr DeviceOperationDefinition kOperations[] = {
    make_device_json_query("list", fill_activity_list),
    device_operation_schemas(
        make_device_json_command("get", handle_activity_get),
        kActivityIdRequest, R"json({"type":"object"})json"),
    device_operation_schemas(
        make_device_json_command("cancel", handle_activity_cancel),
        kActivityIdRequest, R"json({"type":"object"})json"),
};

const DeviceCapabilityDescriptor kCapability{"activity.feedback"};
const DeviceModuleProjectionMetadata kModule{
    "activity", "activity_registry", fill_contract, "LunarLink activities"};
const DeviceModuleProjection kProjection{
    "activity.feedback", &kModule, kOperations, std::size(kOperations)};

bool install_activity_projection(DeviceProjectionRegistry &registry,
                                 std::string &error) {
  return registry.register_projection(
      {&kCapability, &kProjection, nullptr, true}, &error);
}

} // namespace

const StandardProjectionContribution standard_projection::activities{
    "activities", install_activity_projection};

} // namespace lunaredu::lunaros
