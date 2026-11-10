#include "lunaros_usb_frame_dispatcher.h"
#include "device_api/device_api.h"
#include "system/identity.h"
namespace lunaredu::lunaros {
void UsbFrameDispatcher::reset() {
  if (runtime_)
    disconnect_device_frame_transport({*runtime_, DeviceFrameTransport::Usb});
  runtime_ = nullptr;
}
void UsbFrameDispatcher::dispatch(KernelRuntime &runtime,
                                     const DeviceFrame &request,
                                     DeviceFrame &response) {
  runtime_ = &runtime;
  const DeviceFrameContext context{runtime, DeviceFrameTransport::Usb};
  JsonDocument out, input;
  out["ok"] = true;
  const auto type = static_cast<DeviceFrameType>(request.type);
  if (dispatch_device_frame(context, request, response)) {
    if (deserializeJson(out, response.payload)) {
      out.clear();
      out["ok"] = false;
      out["error"]["code"] = "device_function_response_invalid";
    }
  } else if (type == DeviceFrameType::GetInfo) {
    out["protocolVersion"] = kDeviceProtocolVersion;
    out["transport"] = "usb_serial_jtag";
    out["serialNumber"] = hardware_serial_number();
    out["firmwareVersion"] = firmware_version();
    out["provisioning_supported"] = false;
    out["binding_supported"] =
        device_frame_supported(uint8_t(DeviceFrameType::GetBinding));
  } else if (type == DeviceFrameType::GetStatus) {
  } else {
    std::string error;
    if (deserializeJson(input, request.payload) || !input.is<JsonObject>())
      error = "invalid_payload";
    else if (!callbacks_.handle_command ||
             !callbacks_.handle_command(
                 request.type, input.as<JsonVariantConst>(),
                 device_frame_authorized(context, input["nonce"] | uint32_t{0}),
                 out.as<JsonObject>(), error, callbacks_.context))
      error = "bootstrap_unsupported";
    if (!error.empty()) {
      out.clear();
      out["ok"] = false;
      out["error"]["code"] = error;
    }
  }
  if (out["ok"].as<bool>()) {
    if (type == DeviceFrameType::GetInfo && callbacks_.fill_info)
      callbacks_.fill_info(out.as<JsonObject>(), callbacks_.context);
    if (type == DeviceFrameType::GetStatus && callbacks_.fill_status)
      callbacks_.fill_status(out.as<JsonObject>(), callbacks_.context);
  }
  response.version = request.version;
  response.type =
      (out["ok"].as<bool>() ? request.type
                            : uint8_t(DeviceFrameType::Error)) |
      kDeviceFrameResponseFlag;
  response.request_id = request.request_id;
  serializeJson(out, response.payload);
}
} // namespace lunaredu::lunaros
