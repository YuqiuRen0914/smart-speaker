#include "device_api/device_frame_endpoint.h"
#include "device_api/device_api_context.h"
#include "lunaros_network.h"
#include "lunaros_network_wifi_configuration.h"
#include "lunaros_platform_link_setup.h"
#include "platform_link_state.h"
#include "system/identity.h"
#include <ArduinoJson.h>
#include <algorithm>
namespace lunaredu::lunaros {
namespace {
bool supports(uint8_t type) {
  return type == uint8_t(DeviceFrameType::GetBinding) &&
         !platform_link::runtime_config().setup_browser_origin.empty();
}
bool handle(const DeviceFrameContext &context, const DeviceFrame &request,
            DeviceFrame &response) {
  if (!supports(request.type))
    return false;
  JsonDocument input, out;
  out["ok"] = true;
  const auto reject = [&](const char *error) {
    out.clear();
    out["ok"] = false;
    out["error"]["code"] = error;
  };
  const auto *device = platform_link::runtime_device();
  if (!device || &device->runtime != &context.runtime)
    reject("binding_runtime_owner_mismatch");
  else if (deserializeJson(input, request.payload) || !input.is<JsonObject>())
    reject("invalid_payload");
  else if (!(context.transport == DeviceFrameTransport::Usb && input["nonce"].isNull()
                 ? device_frame_channel_confirmed(context)
                 : device_frame_authorized(context, input["nonce"] | uint32_t{0})))
    reject("bootstrap_confirmation_required");
  else {
    const auto network = network_snapshot();
    const auto attempt = network_wifi_connection_snapshot();
    retain_device_frame_reply(context, false);
    out["serialNumber"] = hardware_serial_number();
    if (network.link != NetworkLinkState::Up || network.local_ip.empty() ||
        attempt.phase == NetworkWifiConnectionPhase::Connecting ||
        attempt.phase == NetworkWifiConnectionPhase::Saving)
      out["state"] = "wifi_required";
    else {
      const PlatformLinkSetupPolicy policy{
          platform_link::runtime_config().setup_browser_origin.c_str()};
      const auto binding = platform_link_setup_binding(policy, true);
      out["state"] = binding.state;
      out["error"] = binding.error;
      if (binding.state == "ready") {
        if (binding.code.size() != 6 || binding.expires_at_ms <= 0 ||
            !std::all_of(binding.code.begin(), binding.code.end(),
                         [](char ch) { return ch >= '0' && ch <= '9'; }))
          reject("invalid_binding_response");
        else {
          out["code"] = binding.code;
          out["expiresAtMs"] = binding.expires_at_ms;
          retain_device_frame_reply(context, true);
        }
      } else if (binding.state == "bound")
        retain_device_frame_reply(context, true);
    }
  }
  response.version = request.version;
  response.request_id = request.request_id;
  response.type =
      (out["ok"].as<bool>() ? request.type
                            : uint8_t(DeviceFrameType::Error)) |
      kDeviceFrameResponseFlag;
  serializeJson(out, response.payload);
  return true;
}
const DeviceFrameEndpoint endpoint{handle, nullptr, supports};
} // namespace
bool install_platform_link_setup_endpoint(KernelRuntime &runtime) {
  return install_device_frame_endpoint(runtime, endpoint);
}
void remove_platform_link_setup_endpoint(KernelRuntime &runtime) {
  (void)remove_device_frame_endpoint(runtime, endpoint);
}
} // namespace lunaredu::lunaros
