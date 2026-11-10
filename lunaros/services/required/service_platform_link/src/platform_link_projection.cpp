#include "lunaros_platform_link_service_id.h"
#include "lunaros_platform_link_connection.h"
#include "lunaros_platform_link_projection.h"
#include "lunaros_network_tls.h"
#include "device_api/device_operation_builders.h"
#include "kernel/runtime.h"
#include "model/device_model.h"
#include "device_api/projection_composition.h"
#include "device_api/device_api_context.h"
#include "device_api/device_projection.h"
#include <cstring>
#include <iterator>
#include <string>
#include "lunaros_platform_link.h"
#include "platform_link_ota_retry.h"
#include "kernel/service.h"
#include "lunaros_network.h"
namespace lunaredu::lunaros {
namespace {
void status([[maybe_unused]] DeviceApiContext &device, JsonObject payload) {
  const auto state = platform_link_pairing_snapshot();
  const auto network = network_snapshot();
  payload["ready"] = device.runtime.service_available(kPlatformLinkServiceId);
  payload["online"] = network.link == NetworkLinkState::Up;
  const char *name = "unbound";
  switch (state.state) {
  case PlatformLinkBindingState::Connecting: name = "connecting"; break;
  case PlatformLinkBindingState::Pairing: name = "pairing"; break;
  case PlatformLinkBindingState::Bound: name = "bound"; break;
  case PlatformLinkBindingState::Error: name = "error"; break;
  default: break;
  }
  payload["state"] = name;
  payload["code"] = state.state == PlatformLinkBindingState::Pairing ? state.code : "";
  payload["bind_url"] = state.state == PlatformLinkBindingState::Pairing ? state.bind_url : "";
  payload["expires_at_ms"] = state.expires_at_ms;
  payload["account_name"] = state.account_display_name;
  payload["error"] = state.error;
  payload["persistence_pending"] = state.persistence_pending;
  payload["revocation_requested"] = state.revocation_requested;
  payload["binding_claim_required"] = state.binding_claim_required;
  payload["local_authority_ready"] = platform_link_local_authority_ready();
  const auto unbind = platform_link_unbind_state();
  payload["unbind_state"] = unbind == PlatformLinkUnbindState::Pending ? "pending"
      : unbind == PlatformLinkUnbindState::Succeeded ? "succeeded"
      : unbind == PlatformLinkUnbindState::Failed ? "failed" : "idle";
  const auto check = platform_link_firmware_check_state();
  payload["firmware_check_state"] = check == PlatformLinkFirmwareCheckState::Pending ? "pending"
      : check == PlatformLinkFirmwareCheckState::Succeeded ? "succeeded"
      : check == PlatformLinkFirmwareCheckState::Failed ? "failed" : "idle";
  payload["environment"] = platform_link_environment() == PlatformLinkEnvironment::Development
                                ? "development" : "production";
  payload["environment_restart_pending"] =
      platform_link_environment_restart_pending();
  payload["environment_switch_pending"] = platform_link_environment_switch_pending();
  payload["platform_base_url"] = platform_link_platform_base_url();
  payload["tls_mode"] = network_tls_policy_for_url(platform_link_platform_base_url())->mode;
  const auto platform = platform_link_platform_connection_snapshot();
  const char *platform_state = "disconnected";
  if (platform.state == PlatformLinkPlatformConnectionState::Connected)
    platform_state = "connected";
  else if (platform.state == PlatformLinkPlatformConnectionState::Error)
    platform_state = "error";
  payload["platform_state"] = platform_state;
  payload["platform_http_status"] = platform.status_code;
  payload["platform_error"] = platform.error;
  const auto connection = platform_link_connection_status();
  auto guidance = payload["platform_connection"].to<JsonObject>();
  guidance["phase"] = platform_link_connection_phase_name(connection.phase);
  guidance["error_code"] = platform_link_connection_failure_name(connection.failure);
  guidance["http_status"] = connection.http_status;
  guidance["retry_wait_ms"] = connection.retry_wait_ms;
  guidance["message"] = platform_link_connection_status_text(connection);
  platform_link::fill_automatic_ota_status(payload["automatic_ota"].to<JsonObject>());
}
DeviceOperationResult start([[maybe_unused]] DeviceApiContext &device, JsonVariantConst, JsonObject response) {
  if (!device.runtime.service_available(kPlatformLinkServiceId)) return device_operation_error("platform_link_not_ready", 503);
  if (!request_platform_link_pairing()) return device_operation_error("pairing_unavailable", 409);
  status(device, response);
  auto result = device_operation_ack(); result.status_code = 202; return result;
}
DeviceOperationResult cancel([[maybe_unused]] DeviceApiContext &device, JsonVariantConst, JsonObject response) {
  if (!device.runtime.service_available(kPlatformLinkServiceId)) return device_operation_error("platform_link_not_ready", 503);
  if (!cancel_platform_link_pairing()) return device_operation_error("pairing_not_active", 409);
  status(device, response);
  auto result = device_operation_ack(); result.status_code = 202; return result;
}
DeviceOperationResult update([[maybe_unused]] DeviceApiContext &device, JsonVariantConst, JsonObject response) {
  if (!device.runtime.service_available(kPlatformLinkServiceId)) return device_operation_error("platform_link_not_ready", 503);
  std::string error;
  if (!request_platform_link_firmware_update(error)) return device_operation_error(error.c_str(), 409);
  response["queued"] = true;
  auto result = device_operation_ack(); result.status_code = 202; return result;
}
DeviceOperationResult check_firmware(DeviceApiContext &device, JsonVariantConst, JsonObject response) {
  if (!device.runtime.service_available(kPlatformLinkServiceId)) return device_operation_error("platform_link_not_ready", 503);
  std::string error;
  if (!request_platform_link_firmware_check(error)) return device_operation_error(error.c_str(), 409);
  response["queued"] = true;
  auto result = device_operation_ack(); result.status_code = 202; return result;
}
DeviceOperationResult unbind_account(DeviceApiContext &device, JsonVariantConst request, JsonObject response) {
  if (!device.runtime.service_available(kPlatformLinkServiceId)) return device_operation_error("platform_link_not_ready", 503);
  if (!request["confirm"].is<bool>() || !request["confirm"].as<bool>())
    return device_operation_error("unbind_confirmation_required", 400);
  if (!request_platform_link_unbind()) return device_operation_error("unbind_unavailable", 409);
  status(device, response);
  auto result = device_operation_ack(); result.status_code = 202; return result;
}
DeviceOperationResult set_environment(DeviceApiContext &device,
                                      JsonVariantConst request,
                                      JsonObject response) {
  if (!device.runtime.service_available(kPlatformLinkServiceId)) return device_operation_error("platform_link_not_ready", 503);
  if (!request["confirm"].is<bool>() || !request["confirm"].as<bool>())
    return device_operation_error("environment_confirmation_required", 400);
  const char *environment = request["environment"] | "";
  PlatformLinkEnvironment target;
  if (std::strcmp(environment, "development") == 0) {
    target = PlatformLinkEnvironment::Development;
  } else if (std::strcmp(environment, "production") == 0) {
    target = PlatformLinkEnvironment::Production;
  } else {
    return device_operation_error("invalid_platform_link_environment", 400);
  }
  std::string error;
  if (!request_platform_link_environment(target, error, request["development_base_url"] | ""))
    return device_operation_error(error.c_str(), 409);
  status(device, response);
  response["requested_environment"] = environment;
  auto result = device_operation_ack(); result.status_code = 202; return result;
}
DeviceOperationResult restart_environment(DeviceApiContext &device,
                                          JsonVariantConst,
                                          JsonObject response) {
  std::string error;
  if (!request_platform_link_environment_restart(error))
    return device_operation_error(error.c_str(), 409);
  status(device, response);
  response["restart_queued"] = true;
  auto result = device_operation_ack(); result.status_code = 202; return result;
}
const DeviceCapabilityDescriptor kCapability{"platform_link_control"};
constexpr const char *kEmptySchema =
    R"json({"type":"object","additionalProperties":false})json";
constexpr const char *kEnvironmentSchema =
    R"json({"type":"object","additionalProperties":false,"required":["environment","confirm"],"properties":{"environment":{"type":"string","enum":["development","production"]},"confirm":{"type":"boolean","enum":[true]},"development_base_url":{"type":"string","maxLength":256}}})json";
constexpr const char *kUnbindSchema =
    R"json({"type":"object","additionalProperties":false,"required":["confirm"],"properties":{"confirm":{"type":"boolean","enum":[true]}}})json";
constexpr DeviceOperationDefinition kOperations[] = {
  device_operation_remote(make_device_json_provider("status.get", status), DeviceRemoteAccess::Query),
  make_device_json_command("pairing.start", start),
  make_device_json_command("pairing.cancel", cancel),
  device_operation_schemas(make_device_json_command("binding.unbind", unbind_account), kUnbindSchema, nullptr),
  make_device_json_command("firmware.check", check_firmware),
  device_operation_remote(make_device_json_command("firmware.update", update), DeviceRemoteAccess::Command),
  device_operation_schemas(
      make_device_json_command("environment.set", set_environment),
      kEnvironmentSchema, nullptr),
  device_operation_schemas(
      make_device_json_command("environment.restart", restart_environment),
      kEmptySchema, nullptr),
};
const DeviceModuleProjectionMetadata kModule{"platform_link", "platform_link"};
const DeviceModuleProjection kProjection{"platform_link_control", &kModule, kOperations, std::size(kOperations)};
}
static bool install_platform_link_projection(DeviceProjectionRegistry &registry, std::string &error) {
  return registry.register_projection({&kCapability, &kProjection, nullptr, true}, &error);
}
const StandardProjectionContribution standard_projection::platform_link{"platform_link", install_platform_link_projection};

}
