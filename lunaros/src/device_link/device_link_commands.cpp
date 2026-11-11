#include "device_link/device_link_operations.h"

#include <ctime>

#include <ArduinoJson.h>

#include "lunaredu_lunaros/kernel/log.h"
#include "lunaredu_lunaros/app/app_commands.h"
#include "lunaredu_lunaros/network/network.h"
#include "device_link/device_link_http.h"
#include "kernel/system_primitives.h"
#include "device_link/device_link_state.h"

namespace lunaredu::lunaros::device_link {
namespace {

constexpr const char *kTag = "device_link";

struct CommandExecutionResult {
  bool ok = false;
  bool reboot_after_ack = false;
  std::string error;
  JsonDocument result;
};

CommandExecutionResult execute_command(JsonObjectConst command) {
  CommandExecutionResult execution;
  const std::string type = command["commandType"] | "";
  JsonObject result = execution.result.to<JsonObject>();

  if (type == "status_refresh") {
    add_runtime_state(result["state"].to<JsonObject>());
    result["refreshed"] = true;
    execution.ok = true;
    return execution;
  }

  if (type == "clock_sync") {
    if (!synchronize_network_clock_blocking(
            kDeviceLinkTimeSyncTimeoutMs, execution.error)) {
      return execution;
    }
    result["synchronizedAtEpoch"] = static_cast<int64_t>(time(nullptr));
    execution.ok = true;
    return execution;
  }

  if (type == "led_set") {
    const bool enabled = command["payload"]["enabled"] | false;
    const DeviceLinkConfig &runtime = runtime_config();
    if (runtime.set_status_led == nullptr ||
        !runtime.set_status_led(enabled)) {
      execution.error = "status_led_unavailable";
      return execution;
    }
    result["enabled"] = enabled;
    execution.ok = true;
    return execution;
  }

  if (type == "reboot") {
    result["accepted"] = true;
    execution.ok = true;
    execution.reboot_after_ack = true;
    return execution;
  }

  if (type == "mode_set") {
    const std::string mode = command["payload"]["mode"] | "";
    lunaredu::app::AppControllerStatusSnapshot snapshot;
    if (!lunaredu::app::request_application_switch(mode, snapshot,
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

bool acknowledge_command(const std::string &command_id,
                         const CommandExecutionResult &execution) {
  JsonDocument doc;
  doc["ok"] = execution.ok;
  JsonObject result = doc["result"].to<JsonObject>();
  if (execution.ok) {
    result.set(execution.result.as<JsonObjectConst>());
  } else {
    result["error"] = execution.error.empty() ? "command_failed"
                                              : execution.error;
  }
  std::string body;
  serializeJson(doc, body);
  return perform_request(
      "POST", "/api/hardware/device-runtime/commands/" + command_id, &body,
      nullptr);
}

} // namespace

bool poll_commands() {
  if (!runtime_config().legacy_remote_commands_enabled) return true;
  JsonDocument response;
  if (!perform_request("GET", "/api/hardware/device-runtime/commands", nullptr,
                       &response)) {
    return false;
  }

  JsonArrayConst commands = response["data"]["commands"].as<JsonArrayConst>();
  for (JsonObjectConst command : commands) {
    const std::string command_id = command["id"] | "";
    if (command_id.empty()) {
      continue;
    }
    const CommandExecutionResult execution = execute_command(command);
    if (!acknowledge_command(command_id, execution)) {
      continue;
    }
    LOGI(kTag, "remote command completed: id=%s type=%s ok=%d",
         command_id.c_str(), command["commandType"] | "unknown",
         execution.ok ? 1 : 0);
    if (execution.reboot_after_ack) {
      internal::system_restart_after_delay(300);
    }
  }
  return true;
}

} // namespace lunaredu::lunaros::device_link
