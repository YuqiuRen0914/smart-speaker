#include "diagnostic_checks.h"
#include "diagnostic_inventory.h"
#include "system/system_info.h"
#include "diagnostics/runtime_diagnostics.h"
#include "device_api/device_api_context.h"
#include <cstring>

namespace lunaredu::lunaros::internal {
void fill_diagnostic_checks(DeviceApiContext &, JsonObject response) {
  response["contract_version"] = 1;
  auto items = response["items"].to<JsonArray>();
  for (const char *id : {"operation.preflight", "memory.summary", "resource.owners", "activity.terminal"}) {
    auto item = items.add<JsonObject>();
    item["id"] = id;
    item["mutating"] = false;
    item["starts_driver"] = false;
    item["evidence"] = "configuration_and_software";
  }
}
DeviceOperationResult run_diagnostic_check(DeviceApiContext &device, JsonVariantConst request, JsonObject response) {
  const char *check = request["check"].is<const char *>() ? request["check"].as<const char *>() : "";
  if (!request.is<JsonObjectConst>() || !*check) return device_operation_error("invalid_diagnostic_check", 400);
  const bool preflight = !std::strcmp(check, "operation.preflight");
  const bool memory = !std::strcmp(check, "memory.summary");
  const bool resources = !std::strcmp(check, "resource.owners");
  const bool activity = !std::strcmp(check, "activity.terminal");
  if (!preflight && !memory && !resources && !activity) return device_operation_error("diagnostic_check_not_found", 404);
  for (JsonPairConst field : request.as<JsonObjectConst>()) {
    const char *key = field.key().c_str();
    if (std::strcmp(key, "check") == 0) continue;
    if (preflight && (!std::strcmp(key, "module") || !std::strcmp(key, "operation"))) {
      if (!field.value().is<const char *>()) return device_operation_error("invalid_diagnostic_check", 400);
      const char *text = field.value();
      if (!*text || std::strlen(text) > 128) return device_operation_error("invalid_diagnostic_check", 400);
    } else if (activity && !std::strcmp(key, "activity_id")) {
      if (!field.value().is<const char *>()) return device_operation_error("invalid_diagnostic_check", 400);
      const char *text = field.value();
      if (!*text || std::strlen(text) > 31) return device_operation_error("invalid_diagnostic_check", 400);
    } else if (memory && !std::strcmp(key, "minimum_contiguous_bytes")) {
      if (!field.value().is<unsigned>()) return device_operation_error("invalid_diagnostic_check", 400);
    } else return device_operation_error("invalid_diagnostic_check", 400);
  }
  response["contract_version"] = 1;
  response["check"] = check;
  response["evidence"] = "configuration_and_software";
  response["physical_result"] = "unknown";
  response["consistency"] = "best_effort";
  if (preflight) {
    const char *module = request["module"] | "";
    const char *operation = request["operation"] | "";
    if (!*module || !*operation) return device_operation_error("invalid_diagnostic_check", 400);
    if (!device.model.models.sealed()) return device_operation_error("device_model_not_sealed", 503);
    struct Find { const char *module, *operation; unsigned offset = 0, found_offset = 0; bool found = false; } find{module, operation};
    device.model.models.visit([](const DeviceModuleSpec &m, void *opaque) {
      auto &f = *static_cast<Find *>(opaque);
      for (size_t i = 0; i < m.operation_count; ++i, ++f.offset)
        if (!std::strcmp(f.module, m.id) && !std::strcmp(f.operation, m.operations[i].id)) {
          f.found = true; f.found_offset = f.offset;
        }
    }, &find);
    if (!find.found) return device_operation_error("operation_not_found", 404);
    JsonDocument query, inventory;
    query["offset"] = find.found_offset; query["limit"] = 1;
    const auto read = read_diagnostic_inventory(device, query.as<JsonVariantConst>(), inventory.to<JsonObject>());
    if (!read.ok) return read;
    response["operation"].set(inventory["items"][0]);
    const auto item = inventory["items"][0];
    const char *binding = item["wiring_binding"] | "not_declared";
    response["authorization"] = "not_evaluated_not_a_grant";
    response["conclusion"] = !item["available"].as<bool>() ? "capability_inactive" :
        !std::strcmp(binding, "unconfigured") ? "provider_unconfigured" :
        !std::strcmp(binding, "provider_declared") ? "configuration_available" : "inspection_incomplete";
  } else if (memory) {
    const auto system = lunaredu::system::read_system_info_snapshot();
    const auto runtime = lunaredu::system::read_runtime_diagnostics_snapshot();
    response["unit"] = "bytes";
    response["internal_free_bytes"] = system.internal_free_bytes;
    response["internal_largest_free_bytes"] = runtime.internal_largest_free_bytes;
    if (request["minimum_contiguous_bytes"].isNull()) response["conclusion"] = "observed_without_threshold";
    else {
      const auto minimum = request["minimum_contiguous_bytes"].as<unsigned>();
      response["minimum_contiguous_bytes"] = minimum;
      response["conclusion"] = runtime.internal_largest_free_bytes >= minimum ? "threshold_met" : "threshold_not_met";
    }
    response["allocation_guaranteed"] = false;
  } else if (resources) {
    fill_diagnostic_resources(device, response["resources"].to<JsonObject>());
    response["conclusion"] = "observed";
  } else {
    const char *id = request["activity_id"] | "";
    if (!*id) return device_operation_error("invalid_diagnostic_check", 400);
    struct Find { const char *id; DeviceActivityObservation item; bool found = false; } find{id, {}, false};
    const auto summary = device.model.activities.observe(0, kDeviceActivityCapacity,
        [](const DeviceActivityObservation &item, void *opaque) {
          auto &f = *static_cast<Find *>(opaque);
          if (!std::strcmp(f.id, item.activity_id.data())) { f.item = item; f.found = true; }
        }, &find);
    if (summary.changed) return device_operation_error("activity_changed_during_read", 409);
    if (!find.found) return device_operation_error("activity_not_retained", 404);
    response["activity_id"] = id;
    response["state"] = device_activity_state_name(find.item.state);
    response["request_id"] = find.item.request_id.data();
    response["error_code"] = find.item.error_code.data();
    response["conclusion"] = !device_activity_terminal(find.item.state) ? "pending" :
        find.item.state == DeviceActivityState::Succeeded ? "terminal_success" : "terminal_unsuccessful";
    response["truncated"] = find.item.truncated;
  }
  return device_operation_ack();
}
}
