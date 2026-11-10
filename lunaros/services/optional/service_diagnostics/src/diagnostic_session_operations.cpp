#include "diagnostic_session_operations.h"

#include "diagnostics/session.h"
#include "diagnostics/runtime_diagnostics.h"

namespace lunaredu::lunaros::internal {
namespace {

DeviceOperationResult open_session(DeviceApiContext &device, JsonVariantConst request,
                                   JsonObject response,
                                   const char *failure_message) {
  if (!request["code"].is<const char *>()) {
    return device_operation_error("debug_code_required", 400, failure_message);
  }
  std::string token;
  std::string error;
  if (!open_diagnostic_session(request["code"].as<std::string>(), token,
                               error)) {
    return device_operation_error(error.c_str(),
                                  error == "debug_session_rate_limited" ? 429 : 401,
                                  failure_message);
  }
  response["token"] = token;
  fill_diagnostic_session_status(device, response["session"].to<JsonObject>());
  return device_operation_ack();
}

DeviceOperationResult close_session(DeviceApiContext &device, JsonObject response,
                                    const char *failure_message) {
  const bool closed = close_diagnostic_session();
  if (closed&&system::cpu_monitor_remaining_ms()!=0) system::stop_cpu_monitor();
  fill_diagnostic_session_status(device, response);
  return closed ? device_operation_ack()
                : device_operation_error("debug_session_required", 401,
                                         failure_message);
}

} // namespace

void fill_diagnostic_session_status([[maybe_unused]] DeviceApiContext &device,
                                    JsonObject payload) {
  const auto status = diagnostic_session_status();
  payload["enabled"] = status.enabled;
  payload["active"] = status.active;
  payload["challenge_remaining_ms"] = status.challenge_remaining_ms;
  payload["session_remaining_ms"] = status.session_remaining_ms;
  payload["retry_after_ms"] = status.retry_after_ms;
  payload["challenge_channel"] = "serial";
}

DeviceOperationResult open_system_diagnostic_session(DeviceApiContext &device,
                                                      JsonVariantConst request,
                                                      JsonObject response) {
  return open_session(device, request, response, "diagnostic_session_open_failed");
}

DeviceOperationResult close_system_diagnostic_session(DeviceApiContext &device,
                                                       JsonVariantConst,
                                                       JsonObject response) {
  return close_session(device, response, "diagnostic_session_close_failed");
}

} // namespace lunaredu::lunaros::internal
