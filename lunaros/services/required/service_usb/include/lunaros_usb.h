#pragma once
#include "kernel/service.h"

namespace lunaredu::lunaros {
class DeviceProjectionRegistry;
inline constexpr const char *kUsbServiceId="device.usb";
ServiceNode usb_service_node(DeviceProjectionRegistry &);
using UsbConsoleLineHandler = void (*)(const char *line, void *context);
// Optional product text commands, delivered synchronously on the shared USB
// poll owner. Register before polling; products must not also read the RX ring.
void set_usb_console_line_handler(UsbConsoleLineHandler handler, void *context = nullptr);

// Poll the shared, read-only Device API on the physical USB Serial/JTAG port.
// The shared runtime calls this from the main owner; no network is required.
void poll_usb_device_api(DeviceProjectionRegistry &registry);
}  // namespace lunaredu::lunaros
