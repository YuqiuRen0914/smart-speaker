#include "diagnostic_inventory.h"
#include "bsp/lunaros_board_facts.h"
#include "device_api/device_api_context.h"
#include "device_api/device_projection.h"
#include <esp_timer.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cmath>

namespace lunaredu::lunaros::internal {
namespace {
void decimal(JsonObject object, const char *key, uint64_t value) {
  char text[21];
  std::snprintf(text, sizeof(text), "%llu", static_cast<unsigned long long>(value));
  object[key] = text;
}
void schema(JsonObject item, const char *key, const char *value) {
  // Bound even product-supplied descriptions; never return a silently shortened schema.
  if (!value || std::strlen(value) > 2048) { item[key] = nullptr; return; }
  item[key] = value;
}
void identifier(JsonObject item, const char *key, const char *value) {
  if (value && std::strlen(value) > 128) {
    item[key] = nullptr;
    item["metadata_truncated"] = true;
  } else item[key] = value;
}
}
void fill_diagnostic_activities(DeviceApiContext &device, JsonObject result, size_t offset, size_t limit) {
  result["coverage"] = "retained_activity_registry";
  result["offset"] = offset;
  auto items = result["items"].to<JsonArray>();
  const auto summary = device.model.activities.observe(offset, limit,
      [](const DeviceActivityObservation &activity, void *context) {
        auto &items = *static_cast<JsonArray *>(context);
        auto item = items.add<JsonObject>();
        item["activity_id"] = activity.activity_id.data();
        item["request_id"] = activity.request_id.data();
        item["capability"] = activity.capability_id.data();
        item["operation"] = activity.operation_id.data();
        item["state"] = device_activity_state_name(activity.state);
        item["error_code"] = activity.error_code.data();
        item["revision"] = activity.revision;
        item["truncated"] = activity.truncated;
        if (activity.trace_id) decimal(item, "trace_id", activity.trace_id);
        else item["trace_id"] = nullptr;
      }, &items);
  result["total"] = summary.total;
  result["returned"] = summary.returned;
  result["capacity"] = kDeviceActivityCapacity;
  result["revision_before"] = summary.revision_before;
  result["revision"] = summary.revision_after;
  result["changed_during_read"] = summary.changed;
  const auto next = std::min(offset + summary.returned, summary.total);
  result["next_offset"] = next;
  result["truncated"] = summary.changed || next < summary.total;
}
DeviceOperationResult read_diagnostic_activities(DeviceApiContext &device, JsonVariantConst request, JsonObject result) {
  if (!request.is<JsonObjectConst>()) return device_operation_error("invalid_activity_query", 400);
  for (JsonPairConst field : request.as<JsonObjectConst>()) {
    if ((field.key() != "offset" && field.key() != "limit") || !field.value().is<unsigned>())
      return device_operation_error("invalid_activity_query", 400);
  }
  const unsigned offset = request["offset"] | 0U;
  const unsigned limit = request["limit"] | 8U;
  if (offset > kDeviceActivityCapacity || !limit || limit > 12)
    return device_operation_error("invalid_activity_query", 400);
  result["contract_version"] = 1;
  fill_diagnostic_activities(device, result, offset, limit);
  return device_operation_ack();
}
void fill_diagnostic_resources(DeviceApiContext &device, JsonObject result) {
  result["consistency"] = "best_effort";
  const auto board = board_support_facts();
  auto identity = result["board"].to<JsonObject>();
  identity["id"] = board.id;
  identity["hardware_revision"] = board.hardware_revision;
  identity["support_status"] = board.support_status;
  auto wiring = result["wiring"].to<JsonObject>();
  wiring["source"] = "configured";
  wiring["physical_connection"] = "unknown";
  wiring["coverage"] = "bsp_resource_descriptors";
  const size_t total = board_resource_count();
  wiring["total"] = total;
  auto resources = wiring["items"].to<JsonArray>();
  for (size_t i = 0; i < std::min<size_t>(total, 32); ++i) {
    BoardResourceFacts fact;
    auto item = resources.add<JsonObject>();
    item["index"] = i;
    if (!board_resource_facts(i, fact)) { item["status"] = "unavailable"; continue; }
    item["status"] = "observed";
    item["resource"] = fact.name;
    item["configured_available"] = fact.configured_available;
    item["owner"] = fact.owner[0] ? fact.owner : nullptr;
    auto pins = item["gpio_numbers"].to<JsonArray>();
    for (unsigned pin = 0; pin < 64; ++pin)
      if (fact.pin_mask & (uint64_t{1} << pin)) pins.add(pin);
    auto signals = item["signals"].to<JsonArray>();
    for (size_t signal = 0; signal < std::min<size_t>(fact.signal_count, 8); ++signal) {
      auto observed = signals.add<JsonObject>();
      observed["role"] = fact.signals[signal].role;
      observed["gpio"] = fact.signals[signal].gpio;
    }
    item["signal_roles_status"] = fact.signal_count ? "declared" : "not_declared";
    item["signal_roles_truncated"] = fact.signal_count > signals.size();
  }
  wiring["returned"] = resources.size();
  wiring["truncated"] = total > resources.size();
  OutputControl::Observation output[OutputControl::kOutputCapacity]{};
  const auto now = static_cast<uint32_t>(esp_timer_get_time() / 1000);
  const auto count = device.model.control.observe(output, OutputControl::kOutputCapacity, now);
  auto authority = result["outputs"].to<JsonObject>();
  authority["coverage"] = "output_control";
  authority["total"] = count;
  auto outputs = authority["items"].to<JsonArray>();
  for (size_t i = 0; i < std::min(count, OutputControl::kOutputCapacity); ++i) {
    auto item = outputs.add<JsonObject>();
    const auto &value = output[i];
    item["output"] = value.provider.output;
    item["provider"] = value.provider.id;
    item["owner"] = value.lease.caller;
    decimal(item, "lease_id", value.lease.id);
    item["occupied"] = value.lease.id != 0 || value.pending.id != 0;
    item["usable"] = value.lease.id != 0 && !value.expired && !value.pending.id && !value.retiring;
    item["expired"] = value.expired;
    item["handoff_pending"] = value.pending.id != 0;
    item["retiring"] = value.retiring;
    item["provider_attached"] = value.provider_attached;
    item["pending_owner"] = value.pending.caller;
    decimal(item, "pending_lease_id", value.pending.id);
    item["deadline_ms"] = value.lease.deadline_ms;
    item["maximum_lease_ms"] = value.provider.maximum_lease_ms;
    item["preemptible"] = value.provider.preemptible;
  }
  authority["returned"] = outputs.size();
  authority["truncated"] = count > outputs.size();
}
DeviceOperationResult read_diagnostic_inventory(DeviceApiContext &device, JsonVariantConst request, JsonObject result) {
  if (!request.is<JsonObjectConst>()) return device_operation_error("invalid_inventory_query", 400);
  for (JsonPairConst field : request.as<JsonObjectConst>()) {
    if ((field.key() != "offset" && field.key() != "limit") || !field.value().is<unsigned>())
      return device_operation_error("invalid_inventory_query", 400);
  }
  const unsigned offset = request["offset"] | 0U;
  const unsigned limit = request["limit"] | 8U;
  if (!limit || limit > 16)
    return device_operation_error("invalid_inventory_query", 400);
  if (!device.model.models.sealed()) return device_operation_error("device_model_not_sealed", 503);
  result["contract_version"] = 1;
  result["scope"] = "device_operations";
  result["availability_source"] = "device_capability_registry";
  result["authorization"] = "requirements_only_not_a_grant";
  result["offset"] = offset;
  struct Visit { DeviceApiContext &device; JsonArray items; unsigned offset, limit, total = 0; bool full = false; } visit{
      device, result["items"].to<JsonArray>(), offset, limit};
  device.model.models.visit([](const DeviceModuleSpec &module, void *opaque) {
    auto &v = *static_cast<Visit *>(opaque);
    const bool active = v.device.model.capabilities.active(module.capability_id);
    for (size_t i = 0; i < module.operation_count; ++i) {
      if (v.total++ < v.offset || v.full || v.items.size() >= v.limit) continue;
      const auto &op = module.operations[i];
      auto item = v.items.add<JsonObject>();
      identifier(item, "module", module.id);
      identifier(item, "operation", op.id);
      identifier(item, "capability", module.capability_id);
      item["available"] = active;
      item["unavailable_reason"] = active ? nullptr : "capability_inactive";
      item["mutating"] = op.mutating;
      item["execution"] = op.execution == DeviceOperationExecution::Caller ? "caller" : "owner_executor";
      identifier(item, "owner_executor", op.owner_executor);
      item["cancellable"] = op.cancellable;
      item["diagnostic_access"] = op.diagnostic_access == DeviceDiagnosticAccess::Session ? "session" :
          op.diagnostic_access == DeviceDiagnosticAccess::Bootstrap ? "bootstrap" : "none";
      item["remote_access"] = op.remote_access == DeviceRemoteAccess::Query ? "query" :
          op.remote_access == DeviceRemoteAccess::Command ? "command" : "none";
      schema(item, "request_schema", op.request_schema);
      schema(item, "response_schema", op.response_schema);
      item["schema_complete"] = op.request_schema && op.response_schema &&
          std::strlen(op.request_schema) <= 2048 && std::strlen(op.response_schema) <= 2048;
      item["wiring_binding"] = "not_declared";
      item["physical_result"] = "unknown";
      if (auto *registry = v.device.projections()) {
        const auto *projection = registry->find_operation(module.id, op.id);
        if (projection) {
          const auto method = projection->method;
          item["http_method"] = method == DeviceOperationMethod::Get ? "GET" :
              method == DeviceOperationMethod::Post ? "POST" :
              method == DeviceOperationMethod::Put ? "PUT" : "DELETE";
          item["maximum_request_bytes"] = projection->maximum_request_bytes;
          if (projection->diagnostic_provider || projection->diagnostic_binding) {
            DeviceOperationDiagnosticFacts facts;
            const auto *binding = projection->diagnostic_binding;
            const bool observed = binding ? binding->observe && binding->observe(binding->context, facts) :
                projection->diagnostic_provider(v.device, facts);
            if (!observed) {
              item["wiring_binding"] = "unconfigured";
            } else if (facts.signal_count > facts.signals.size() || facts.parameter_count > facts.parameters.size()) {
              item["wiring_binding"] = "invalid_provider_metadata";
            } else {
              item["wiring_binding"] = "provider_declared";
              auto signals = item["signals"].to<JsonArray>();
              for (size_t j = 0; j < facts.signal_count; ++j) {
                const auto &signal = facts.signals[j];
                auto observed = signals.add<JsonObject>();
                observed["source"] = "configured";
                identifier(observed, "role", signal.role);
                identifier(observed, "resource", signal.resource);
                if (signal.gpio >= 0 && signal.gpio < 64) observed["gpio"] = signal.gpio;
                else observed["gpio"] = nullptr;
                if (signal.active_low_known) observed["active_low"] = signal.active_low;
                else observed["active_low"] = nullptr;
              }
              auto parameters = item["parameter_details"].to<JsonArray>();
              for (size_t j = 0; j < facts.parameter_count; ++j) {
                const auto &parameter = facts.parameters[j];
                auto observed = parameters.add<JsonObject>();
                identifier(observed, "name", parameter.name);
                identifier(observed, "unit", parameter.unit);
                const bool valid_range = parameter.range_known && std::isfinite(parameter.minimum) &&
                    std::isfinite(parameter.maximum) && parameter.minimum <= parameter.maximum;
                observed["range_status"] = valid_range ? "declared" :
                    parameter.range_known ? "invalid_provider_metadata" : "unknown";
                if (valid_range) {
                  observed["minimum"] = parameter.minimum;
                  observed["maximum"] = parameter.maximum;
                }
              }
            }
          }
        }
      }
      // Keep the serialized page bounded even when schemas are large.
      // A single escaped schema can consume the entire budget. Preserve a small
      // item and advance the cursor rather than returning the same empty page.
      if (v.items.size() == 1 && measureJson(v.items) > 8192) {
        item["request_schema"] = nullptr;
        item["response_schema"] = nullptr;
        item["schema_complete"] = false;
        item["metadata_truncated"] = true;
        if (measureJson(v.items) > 8192) {
          item.remove("signals");
          item.remove("parameter_details");
          item["wiring_binding"] = "metadata_omitted_for_page_budget";
        }
      }
      if (measureJson(v.items) > 8192) {
        v.items.remove(v.items.size() - 1);
        v.full = true;
      }
    }
  }, &visit);
  result["total"] = visit.total;
  result["returned"] = visit.items.size();
  const auto next = std::min<size_t>(offset + visit.items.size(), visit.total);
  result["next_offset"] = next;
  result["truncated"] = next < visit.total;
  result["capability_revision"] = device.model.capabilities.revision();
  return device_operation_ack();
}
}
