#pragma once
#include "device_api/device_frame_endpoint.h"
#include <ArduinoJson.h>
#include <string>
namespace lunaredu::lunaros {
struct UsbFrameCallbacks {
  void *context = nullptr;
  void (*fill_info)(JsonObject, void *) = nullptr;
  void (*fill_status)(JsonObject, void *) = nullptr;
  bool (*handle_command)(uint8_t, JsonVariantConst, bool confirmed, JsonObject,
                         std::string &, void *) = nullptr;
};
class UsbFrameDispatcher {
public:
  explicit UsbFrameDispatcher(UsbFrameCallbacks callbacks = {})
      : callbacks_(callbacks) {}
  void reset();
  void dispatch(KernelRuntime &, const DeviceFrame &,
                DeviceFrame &);

private:
  UsbFrameCallbacks callbacks_;
  KernelRuntime *runtime_ = nullptr;
};
} // namespace lunaredu::lunaros
