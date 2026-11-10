#include "lunaros_network_control.h"
#include "device_api/device_api_context.h"
#include "platform_link_operations.h"

#include <ctime>
#include <algorithm>
#include <vector>

#include <ArduinoJson.h>

#include <esp_log.h>
#include "device_api/adapters/application_projection.h"
#include "device_api/operation_dispatch.h"
#include "device_api/device_operation.h"
#include "device_api/device_projection.h"
#include "device_api/operation_feedback.h"
#include "platform_link_http.h"
#include "platform/system.h"
#include "platform_link_state.h"
#include "platform_link_wire.h"
#include "system/identity.h"

namespace lunaredu::lunaros::platform_link {
namespace {

constexpr const char *kTag = "platform_link";

struct CommandExecutionResult {
  bool ok = false;
  bool reboot_after_ack = false;
  std::string error;
  JsonDocument result;
};

struct PendingReceipt {
  std::string command_id;
  std::string command_type;
  std::string body;
  bool ok = false;
  bool reboot_after_ack = false;
  int binding_epoch = 0;
};

std::vector<PendingReceipt> g_pending_receipts;

struct RemoteInvocation {
  DeviceApiContext &device;
  const DeviceOperationProjection &binding;
  JsonVariantConst request;
  JsonObject response;
  DeviceOperationResult result;
};

void invoke_remote_json(void *opaque, ServiceOperationResult &status) {
  auto &call = *static_cast<RemoteInvocation *>(opaque);
  const auto &binding = call.binding;
  if (binding.json_binding != nullptr) {
    call.result = binding.json_binding->invoke(binding.json_binding->context,
                                              call.request, call.response);
  } else if (binding.handler_kind == DeviceOperationHandlerKind::JsonQuery) {
    if (binding.json_result_provider != nullptr)
      call.result = binding.json_result_provider(call.device, call.response);
    else if (binding.json_provider != nullptr)
      binding.json_provider(call.device, call.response);
    else call.result = {false, 501, "json_provider_unavailable"};
  } else if (binding.handler_kind == DeviceOperationHandlerKind::JsonCommand) {
    call.result = binding.json_command_handler != nullptr
        ? binding.json_command_handler(call.device, call.request, call.response)
        : DeviceOperationResult{false, 501, "json_command_handler_unavailable"};
  } else call.result = {false, 400, "remote_json_required"};
  status.work_result_known = true;
  status.work_ok = call.result.ok;
  status.work_error_code = call.result.error.c_str();
  status.ok = true;
}

CommandExecutionResult execute_device_operation(JsonObjectConst command) {
  CommandExecutionResult execution;
  DeviceApiContext *device = runtime_device();
  auto *registry = device != nullptr ? device->projections() : nullptr;
  const std::string module = command["module_id"] | "";
  const std::string operation = command["operation_id"] | "";
  if (registry == nullptr || module.empty() || operation.empty()) {
    execution.error = "remote_operation_invalid";
    return execution;
  }
  const DeviceModuleSpec *spec = device->model.models.find_module(module.c_str());
  const DeviceOperationSpec *model = device->model.models.find_operation(module.c_str(), operation.c_str());
  const DeviceOperationProjection *binding = registry->find_operation(module.c_str(), operation.c_str());
  if (spec == nullptr || model == nullptr || binding == nullptr ||
      !device->model.capabilities.active(spec->capability_id)) {
    execution.error = "remote_capability_unavailable";
    return execution;
  }
  const bool query = model->remote_access == DeviceRemoteAccess::Query &&
      model->kind == DeviceOperationKind::Query && !model->mutating &&
      binding->handler_kind == DeviceOperationHandlerKind::JsonQuery;
  const bool mutation = model->remote_access == DeviceRemoteAccess::Command &&
      model->kind == DeviceOperationKind::Command && model->mutating &&
      binding->handler_kind == DeviceOperationHandlerKind::JsonCommand;
  if (!query && !mutation) {
    execution.error = "remote_operation_not_allowed";
    return execution;
  }
  const JsonVariantConst request = command["request"];
  if (!request.is<JsonObjectConst>() || measureJson(request) > wire::request_limit(module,operation) ||
      (binding->maximum_request_bytes && measureJson(request) > binding->maximum_request_bytes)) {
    execution.error = "remote_request_invalid";
    return execution;
  }
  JsonObject data = execution.result["data"].to<JsonObject>();
  RemoteInvocation call{*device, *binding, request, data, {}};
  auto correlation = begin_device_operation_feedback(*device);
  std::string dispatch_error;
  const bool dispatched = dispatch_device_operation(device->runtime, *model, invoke_remote_json,
                                           &call, dispatch_error, module.c_str(), &correlation);
  if (!dispatched) call.result = {false, 503, dispatch_error};
  finish_device_operation_feedback(*device, *spec, *model, call.result, data,
      execution.result.as<JsonObject>(), correlation);
  if (!dispatched) {
    execution.error = dispatch_error.empty() ? "operation_owner_unavailable" : dispatch_error;
    return execution;
  }
  if (!call.result.ok) {
    execution.error = call.result.error.empty() ? "remote_operation_failed" : call.result.error;
    return execution;
  }
  if (measureJson(execution.result) > protocol::contract::operation_max_result_bytes) {
    execution.result.clear();
    execution.error = "remote_response_too_large";
    return execution;
  }
  execution.ok = true;
  return execution;
}

CommandExecutionResult execute_command(JsonObjectConst command) {
  CommandExecutionResult execution;
  const std::string type = command["operation_id"] | "";
  JsonObject result = execution.result.to<JsonObject>();

  if (command["module_id"] != protocol::contract::builtin_module.data()) return execute_device_operation(command);

  if (type == "status.get") {
    add_runtime_state(result["state"].to<JsonObject>());
    result["refreshed"] = true;
    execution.ok = true;
    return execution;
  }

  if (type == "clock.sync") {
    if (!synchronize_network_clock_blocking(
            kPlatformLinkTimeSyncTimeoutMs, execution.error)) {
      return execution;
    }
    result["synchronizedAtEpoch"] = static_cast<int64_t>(time(nullptr));
    execution.ok = true;
    return execution;
  }

  if (type == "led.set") {
    const bool enabled = command["request"]["enabled"] | false;
    const PlatformLinkConfig &runtime = runtime_config();
    if (runtime.set_status_led == nullptr ||
        !runtime.set_status_led(enabled)) {
      execution.error = "status_led_unavailable";
      return execution;
    }
    result["enabled"] = enabled;
    execution.ok = true;
    return execution;
  }

  if (type == "restart") {
    result["accepted"] = true;
    execution.ok = true;
    execution.reboot_after_ack = true;
    return execution;
  }

  if (type == "application.switch") {
    const std::string mode = command["request"]["mode"] | "";
    lunaredu::app::AppControllerStatusSnapshot snapshot;
    auto *runtime = runtime_device();
    if (runtime == nullptr) {
      execution.error = "platform_link_not_configured";
      return execution;
    }
    if (!device_api_switch_application(*runtime, mode, snapshot,
                                                   execution.error)) {
      return execution;
    }
    result["mode"] = snapshot.active_application_id;
    result["pendingMode"] = snapshot.pending_application_id;
    execution.ok = true;
    return execution;
  }

  execution.error = "unsupported_remote_command";
  return execution;
}

PendingReceipt make_receipt(JsonObjectConst request,
                            const CommandExecutionResult &execution) {
  JsonDocument doc;
  wire::result(doc,request,execution.ok,execution.result.as<JsonObjectConst>(),execution.error);
  PendingReceipt receipt;
  receipt.command_id = wire::text(request["request_id"]);
  receipt.command_type = wire::text(request["payload"]["operation_id"]);
  receipt.ok = doc["payload"]["ok"].as<bool>();
  receipt.reboot_after_ack = execution.reboot_after_ack && receipt.ok;
  receipt.binding_epoch = request["session"]["binding_epoch"].as<int>();
  serializeJson(doc,receipt.body);
  return receipt;
}

bool execute_realtime_teleop_internal(JsonObjectConst frame, JsonDocument &reply) {
  const auto invocation=frame["payload"].as<JsonObjectConst>();
  const std::string operation=wire::text(invocation["operation_id"]);
  if (invocation["module_id"]!="desktop_pet" || (operation!="teleop.begin" && operation!="teleop.setpoint" &&
      operation!="teleop.stop" && operation!="teleop.state.get")) {
    reply["error"]="invalid_teleop_operation";return false;
  }
  const auto result=execute_device_operation(invocation);
  if (!result.ok) {reply["error"]=result.error;return false;}
  reply["data"].set(result.result["data"]);
  return true;
}

bool flush_receipts() {
  while (!g_pending_receipts.empty()) {
    const size_t count = std::min<size_t>(protocol::contract::operation_max_batch_size, g_pending_receipts.size());
    JsonDocument batch;
    auto receipts = batch["receipts"].to<JsonArray>();
    for (size_t i = 0; i < count; ++i) {
      JsonDocument item;
      if (deserializeJson(item, g_pending_receipts[i].body)) return false;
      receipts.add(item.as<JsonObjectConst>());
    }
    std::string body;
    serializeJson(batch, body);
    JsonDocument response;
    if (!perform_request("POST", "/api/link/device-runtime/commands/receipts",
                         &body, &response)) return false;
    const auto confirmations = response["data"]["receipts"].as<JsonArrayConst>();
    if (confirmations.size() != count) return false;
    // Confirm every ID before retiring any local result. A lost/invalid
    // response retains the same receipts for an idempotent upload retry.
    bool reboot = false;
    for (size_t i = 0; i < count; ++i) {
      const auto &receipt = g_pending_receipts[i];
      size_t matches = 0;
      bool accepted = false;
      for (JsonObjectConst confirmation : confirmations) {
        if (receipt.command_id != (confirmation["request_id"] | "")) continue;
        if (!confirmation["accepted"].is<bool>()) return false;
        ++matches;
        accepted = confirmation["accepted"].as<bool>();
      }
      if (matches != 1) return false;
      ESP_LOGI(kTag, "remote command receipt confirmed: id=%s type=%s accepted=%d ok=%d",
          receipt.command_id.c_str(), receipt.command_type.c_str(), accepted ? 1 : 0, receipt.ok ? 1 : 0);
      const auto current=pairing_snapshot();
      reboot = reboot || (accepted && receipt.reboot_after_ack &&
          current.binding_epoch==receipt.binding_epoch && current.state==PlatformLinkBindingState::Bound &&
          !current.revocation_requested && !current.persistence_pending);
    }
    g_pending_receipts.erase(g_pending_receipts.begin(), g_pending_receipts.begin() + count);
    if (reboot) system_restart_after_delay(300);
  }
  return true;
}

} // namespace

bool execute_realtime_teleop(JsonObjectConst frame, JsonDocument &reply) {
  return execute_realtime_teleop_internal(frame, reply);
}

bool poll_commands() {
  if (!runtime_config().remote_commands_enabled) return true;
  const auto admitted=pairing_snapshot();
  if(admitted.state!=PlatformLinkBindingState::Bound || admitted.revocation_requested || admitted.persistence_pending) {
    g_pending_receipts.clear();
    return true;
  }
  g_pending_receipts.erase(std::remove_if(g_pending_receipts.begin(),g_pending_receipts.end(),
      [&](const PendingReceipt &receipt){return receipt.binding_epoch!=admitted.binding_epoch;}),g_pending_receipts.end());
  if (!flush_receipts()) return false;
  JsonDocument response;
  if (!perform_request("GET", "/api/link/device-runtime/commands", nullptr,
                       &response)) {
    return false;
  }

  if (!response["data"]["commands"].is<JsonArrayConst>()) return false;
  JsonArrayConst commands = response["data"]["commands"].as<JsonArrayConst>();
  if (commands.size()>protocol::contract::operation_max_batch_size) return false;
  for (size_t i=0;i<commands.size();++i) {
    if (!wire::operation(commands[i],"durable")) return false;
    for(size_t j=0;j<i;++j) if(commands[i]["request_id"]==commands[j]["request_id"])return false;
  }
  for (JsonObjectConst command : commands) {
    const auto binding=pairing_snapshot();
    if (!wire::operation(command,"durable") || binding.state!=PlatformLinkBindingState::Bound ||
        binding.revocation_requested || binding.persistence_pending ||
        command["session"]["binding_epoch"].as<int>()!=binding.binding_epoch ||
        command["session"]["id"]!=protocol::device_session_id(hardware_serial_number())) return false;
    CommandExecutionResult execution;
    if (wire::deadline_live(command["payload"]["expires_at"]))
      execution=execute_command(command["payload"].as<JsonObjectConst>());
    else execution.error="operation_expired_or_clock_unavailable";
    g_pending_receipts.push_back(make_receipt(command,execution));
  }
  return flush_receipts();
}

} // namespace lunaredu::lunaros::platform_link
