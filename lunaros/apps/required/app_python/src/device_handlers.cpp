#include "device_handlers.h"
#include "script_catalog.h"
#include "device_api/device_api_context.h"
#include "control_dispatch.h"

#include "device_api/device_operation_types.h"
#include "lunaros_micropython_native_modules.h"
#include "lunaros_micropython_config.h"

using namespace lunaredu::lunaros;

DeviceOperationResult handle_micropython_script_upload_bulk_payload(
    lunaredu::lunaros::DeviceApiContext &device, const DeviceBulkRequest &request, JsonObject response_payload) {
  if (request.body_size > kMicroPythonMaxUploadPayloadBytes) {
    return device_operation_error("script_too_large", 413,
                                     "micropython_operation_failed");
  }

  MicroPythonRuntimeStatus status;
  std::string error;
  const char *name_value = request.payload["name"] | "";
  const std::string name = name_value != nullptr ? std::string(name_value)
                                                 : std::string();
  const bool set_autorun = request.payload["autorun"] | false;
  const bool run_after_upload = request.payload["run"] | false;

  JsonDocument document;
  document["content"] = request.payload["content"] | "";
  if (!dispatch_micropython_upload_script(device.runtime, name, document, set_autorun,
                                          run_after_upload, &status, error)) {
    return device_operation_error(error.c_str(), 400,
                                     "micropython_operation_failed");
  }

  response_payload["ok"] = true;
  response_payload["name"] = name.c_str();
  fill_micropython_runtime_status_payload(
      response_payload["runtime"].to<JsonObject>(), status);
  return device_operation_ack();
}

void fill_micropython_capabilities_payload(
    lunaredu::lunaros::DeviceApiContext &, JsonObject payload) {
  static const char *const kSupportedOps[] = {
      "python_script",
      "repl",
      "script_upload",
      "script_run",
      "script_stop",
      "script_autorun",
      "project_upload",
      "stdout_logs",
  };

  static const char *const kEnabledModules[] = {
      "sys",
      "io",
      "gc",
      "math",
      "collections",
      "array",
      "struct",
  };

  payload["ok"] = true;
  payload["runtime"] = "micropython.embed";
  payload["script_root"] = lunaredu::python::scripts::kScriptRoot;
  payload["autorun_path"] = lunaredu::python::scripts::kAutorunPath;
  payload["default_script_path"] = lunaredu::python::scripts::kDefaultScriptPath;
  payload["heap_bytes"] = kMicroPythonHeapBytes;
  payload["tick_interval_ms"] = kMicroPythonTickIntervalMs;
  payload["task_stack_bytes"] = kMicroPythonTaskStackBytes;
  payload["task_priority"] = kMicroPythonTaskPriority;
  payload["vm_yield_branch_interval"] = kMicroPythonVmYieldBranchInterval;
  payload["command_queue_depth"] = kMicroPythonCommandQueueDepth;
  payload["supports_sync_exec"] = false;
  payload["supports_async_exec"] = true;
  payload["supports_stop"] = true;
  payload["supports_repl"] = true;
  payload["supports_autorun"] = true;
  payload["supports_lunaredu_bindings"] =
      micropython_native_module_registered("lunaredu");
  payload["log_ring_size"] = kMicroPythonMaxLogs;
  JsonArray ops = payload["supported_ops"].to<JsonArray>();
  for (const char *item : kSupportedOps) {
    ops.add(item);
  }
  JsonArray modules = payload["enabled_modules"].to<JsonArray>();
  modules.add("lunar_project");
  for (const char *item : kEnabledModules) {
    modules.add(item);
  }
  visit_micropython_native_modules(
      [](const MicroPythonNativeModuleDescriptor &descriptor, void *context) {
        JsonArray *target = static_cast<JsonArray *>(context);
        if (target != nullptr && descriptor.name != nullptr) {
          target->add(descriptor.name);
        }
      },
      &modules);
}

void fill_micropython_runtime_status_payload(
    JsonObject payload, const MicroPythonRuntimeStatus &status) {
  payload["ok"] = true;
  payload["available"] = status.available;
  payload["initialized"] = status.initialized;
  payload["entered"] = status.entered;
  payload["running"] = status.running;
  payload["stop_requested"] = status.stop_requested;
  payload["repl_active"] = status.repl_active;
  payload["foreground"] = status.foreground;
  payload["application_id"] = status.application_id;
  payload["state"] = status.state;
  payload["last_error"] = status.last_error;
  payload["current_program"] = status.current_program;
  std::string autorun, autorun_error;
  if (lunaredu::python::scripts::autorun(autorun,autorun_error)) payload["autorun_script"] = autorun;
  else { payload["autorun_script"] = nullptr; payload["autorun_error"] = autorun_error; }
  payload["repl_mode"] = status.repl_mode;
  payload["started_at_ms"] = status.started_at_ms;
  payload["completed_at_ms"] = status.completed_at_ms;
  payload["log_seq"] = status.log_seq;
  payload["execution_id"] = status.execution_id;
}

DeviceOperationResult fill_micropython_runtime_status_payload(
    lunaredu::lunaros::DeviceApiContext &device, JsonObject payload) {
  MicroPythonRuntimeStatus status;
  std::string error;
  if (!dispatch_micropython_runtime_status(device.runtime, &status, error)) {
    return device_operation_error(
        error.empty() ? "micropython_status_unavailable" : error.c_str(), 500,
        "micropython_operation_failed");
  }

  fill_micropython_runtime_status_payload(payload, status);
  return DeviceOperationResult();
}

DeviceOperationResult handle_micropython_runtime_enter(
    lunaredu::lunaros::DeviceApiContext &device, JsonVariantConst request_payload, JsonObject response_payload) {
  (void)request_payload;

  MicroPythonRuntimeStatus status;
  std::string error;
  if (!dispatch_micropython_runtime_enter(device.runtime, &status, error)) {
    return device_operation_error(
        error.empty() ? "micropython_runtime_enter_failed" : error.c_str(),
        500, "micropython_operation_failed");
  }

  fill_micropython_runtime_status_payload(response_payload, status);
  return device_operation_ack();
}

DeviceOperationResult handle_micropython_runtime_leave(
    lunaredu::lunaros::DeviceApiContext &device, JsonVariantConst request_payload, JsonObject response_payload) {
  (void)request_payload;

  MicroPythonRuntimeStatus status;
  std::string error;
  if (!dispatch_micropython_runtime_leave(device.runtime, &status, error)) {
    return device_operation_error(
        error.empty() ? "micropython_runtime_leave_failed" : error.c_str(),
        500, "micropython_operation_failed");
  }

  fill_micropython_runtime_status_payload(response_payload, status);
  return device_operation_ack();
}

DeviceOperationResult handle_micropython_repl_enter(
    lunaredu::lunaros::DeviceApiContext &device, JsonVariantConst request_payload, JsonObject response_payload) {
  (void)request_payload;

  MicroPythonRuntimeStatus status;
  std::string error;
  if (!dispatch_micropython_repl_enter(device.runtime, &status, error)) {
    return device_operation_error(
        error.empty() ? "micropython_repl_enter_failed" : error.c_str(), 500,
        "micropython_operation_failed");
  }

  fill_micropython_runtime_status_payload(response_payload, status);
  return device_operation_ack();
}

DeviceOperationResult handle_micropython_repl_leave(
    lunaredu::lunaros::DeviceApiContext &device, JsonVariantConst request_payload, JsonObject response_payload) {
  (void)request_payload;

  MicroPythonRuntimeStatus status;
  std::string error;
  if (!dispatch_micropython_repl_leave(device.runtime, &status, error)) {
    return device_operation_error(
        error.empty() ? "micropython_repl_leave_failed" : error.c_str(), 500,
        "micropython_operation_failed");
  }

  fill_micropython_runtime_status_payload(response_payload, status);
  return device_operation_ack();
}

DeviceOperationResult handle_micropython_repl_feed(
    lunaredu::lunaros::DeviceApiContext &device, JsonVariantConst request_payload, JsonObject response_payload) {
  JsonDocument command;
  command["input"] = request_payload["input"] | "";

  MicroPythonRuntimeStatus status;
  std::string error;
  if (!dispatch_micropython_repl_feed(device.runtime, command, &status, error)) {
    return device_operation_error(
        error.empty() ? "micropython_repl_feed_failed" : error.c_str(), 400,
        "micropython_operation_failed");
  }

  fill_micropython_runtime_status_payload(response_payload, status);
  return device_operation_ack();
}

DeviceOperationResult fill_micropython_runtime_logs_payload(
    lunaredu::lunaros::DeviceApiContext &device, JsonVariantConst request_payload, JsonObject response_payload) {
  const uint32_t after_seq =
      static_cast<uint32_t>(request_payload["after_seq"] | 0);

  JsonDocument document;
  std::string error;
  if (!dispatch_micropython_fill_logs_payload(device.runtime, document, after_seq, error)) {
    return device_operation_error(
        error.empty() ? "micropython_logs_unavailable" : error.c_str(), 500,
        "micropython_operation_failed");
  }

  response_payload.set(document["payload"]);
  return DeviceOperationResult();
}

DeviceOperationResult fill_micropython_script_list_payload(
    lunaredu::lunaros::DeviceApiContext &device, JsonObject payload) {
  JsonDocument document;
  std::string error;
  if (!dispatch_micropython_fill_script_list_payload(device.runtime, document, error)) {
    return device_operation_error(
        error.empty() ? "micropython_script_list_unavailable" : error.c_str(),
        500, "micropython_operation_failed");
  }

  payload.set(document["payload"]);
  return DeviceOperationResult();
}

DeviceOperationResult handle_micropython_script_run(
    lunaredu::lunaros::DeviceApiContext &device, JsonVariantConst request_payload, JsonObject response_payload) {
  MicroPythonRuntimeStatus status;
  std::string error;
  if (!dispatch_micropython_run_script(device.runtime, std::string(request_payload["name"] | ""),
                                       &status, error)) {
    return device_operation_error(
        error.empty() ? "micropython_script_run_failed" : error.c_str(), 400,
        "micropython_operation_failed");
  }

  fill_micropython_runtime_status_payload(response_payload, status);
  return device_operation_ack();
}

DeviceOperationResult handle_micropython_script_stop(
    lunaredu::lunaros::DeviceApiContext &device, JsonVariantConst request_payload, JsonObject response_payload) {
  const std::string reason =
      std::string(request_payload["reason"] | "local_api");
  MicroPythonRuntimeStatus status;
  std::string error;
  if (!dispatch_micropython_stop(device.runtime, reason, &status, error)) {
    return device_operation_error(
        error.empty() ? "micropython_script_stop_failed" : error.c_str(), 500,
        "micropython_operation_failed");
  }

  fill_micropython_runtime_status_payload(response_payload, status);
  return device_operation_ack();
}

DeviceOperationResult handle_micropython_script_delete(
    lunaredu::lunaros::DeviceApiContext &device, JsonVariantConst request_payload, JsonObject response_payload) {
  std::string error;
  if (!dispatch_micropython_delete_script(device.runtime, std::string(request_payload["name"] | ""),
                                          error)) {
    return device_operation_error(
        error.empty() ? "micropython_script_delete_failed" : error.c_str(),
        400, "micropython_operation_failed");
  }

  JsonDocument document;
  if (!dispatch_micropython_fill_script_list_payload(device.runtime, document, error)) {
    return device_operation_error(
        error.empty() ? "micropython_script_list_unavailable" : error.c_str(),
        500, "micropython_operation_failed");
  }

  response_payload.set(document["payload"]);
  return device_operation_ack();
}

DeviceOperationResult handle_micropython_script_autorun_set(
    lunaredu::lunaros::DeviceApiContext &device, JsonVariantConst request_payload, JsonObject response_payload) {
  std::string error;
  if (!dispatch_micropython_set_autorun(device.runtime, std::string(request_payload["name"] | ""),
                                        error)) {
    return device_operation_error(
        error.empty() ? "micropython_autorun_set_failed" : error.c_str(), 400,
        "micropython_operation_failed");
  }

  JsonDocument document;
  if (!dispatch_micropython_fill_script_list_payload(device.runtime, document, error)) {
    return device_operation_error(
        error.empty() ? "micropython_script_list_unavailable" : error.c_str(),
        500, "micropython_operation_failed");
  }

  response_payload.set(document["payload"]);
  return device_operation_ack();
}
