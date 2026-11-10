#include "lunaredu_lunaros/device_api/device_projection_json.h"
#include <iterator>
#include "lunaredu_lunaros/device_api/device_projection.h"
#include "lunaredu_lunaros/device_link/device_link.h"
#include "lunaredu_lunaros/kernel/service_registry.h"
#include "lunaredu_lunaros/kernel/service_ids.h"
#include "lunaredu_lunaros/network/network.h"
namespace lunaredu::lunaros {
namespace {
void status(JsonObject payload) {
  const auto state = device_link_pairing_snapshot();
  const auto network = network_snapshot();
  payload["ready"] = service_available(kDeviceLinkServiceId);
  payload["online"] = network.link == NetworkLinkState::Up;
  const char *name = "unbound";
  switch (state.state) {
  case DeviceLinkBindingState::Connecting: name = "connecting"; break;
  case DeviceLinkBindingState::Pairing: name = "pairing"; break;
  case DeviceLinkBindingState::Bound: name = "bound"; break;
  case DeviceLinkBindingState::Error: name = "error"; break;
  default: break;
  }
  payload["state"] = name;
  payload["code"] = state.state == DeviceLinkBindingState::Pairing ? state.code : "";
  payload["bind_url"] = state.state == DeviceLinkBindingState::Pairing ? state.bind_url : "";
  payload["expires_at_ms"] = state.expires_at_ms;
  payload["account_name"] = state.account_display_name;
  payload["error"] = state.error;
}
DeviceOperationResult start(JsonVariantConst, JsonObject response) {
  if (!service_available(kDeviceLinkServiceId)) return device_operation_error("device_link_not_ready", 503);
  if (!request_device_link_pairing()) return device_operation_error("pairing_unavailable", 409);
  status(response);
  auto result = device_operation_ack(); result.status_code = 202; return result;
}
DeviceOperationResult cancel(JsonVariantConst, JsonObject response) {
  if (!service_available(kDeviceLinkServiceId)) return device_operation_error("device_link_not_ready", 503);
  if (!cancel_device_link_pairing()) return device_operation_error("pairing_not_active", 409);
  status(response);
  auto result = device_operation_ack(); result.status_code = 202; return result;
}
DeviceOperationResult update(JsonVariantConst, JsonObject response) {
  if (!service_available(kDeviceLinkServiceId)) return device_operation_error("device_link_not_ready", 503);
  std::string error;
  if (!request_device_link_firmware_update(error)) return device_operation_error(error.c_str(), 409);
  response["queued"] = true;
  auto result = device_operation_ack(); result.status_code = 202; return result;
}
const DeviceCapabilityDescriptor kCapability{"device_link_control"};
constexpr DeviceOperationDefinition kOperations[] = {
  make_device_json_provider("status.get", status),
  make_device_json_command("pairing.start", start),
  make_device_json_command("pairing.cancel", cancel),
  make_device_json_command("firmware.update", update),
};
const DeviceModuleProjectionMetadata kModule{"device_link", "device_link"};
const DeviceModuleProjection kProjection{"device_link_control", &kModule, kOperations, std::size(kOperations)};
}
bool setup_device_link_projection(std::string &error) {
  return register_device_projection({&kCapability, &kProjection, nullptr, true}, &error);
}
}
