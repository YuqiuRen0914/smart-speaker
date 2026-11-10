#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
namespace lunaredu::lunaros {
class KernelRuntime;
class DeviceProjectionRegistry;
struct UsbHostSnapshot {
  bool running = false, connected = false;
  uint32_t generation = 0;
  uint64_t dropped_bytes = 0;
  int error = 0;
};
// Lifecycle calls run on lunar.main. Ownership stays held during asynchronous
// shutdown; release returns false until the managed USB worker has retired.
bool start_usb_cdc_host(KernelRuntime &, DeviceProjectionRegistry &, std::string &error);
bool stop_usb_cdc_host(std::string &error);
void usb_cdc_host_snapshot_into(UsbHostSnapshot &);
// Exactly one I/O owner; byte reads and writes are serialized with reconnect.
size_t usb_cdc_host_read(uint32_t generation, uint8_t *, size_t);
// One reader may block on a static binary semaphore, woken by RX/disconnect.
// Does not consume bytes. Returns false on timeout or a retired generation.
bool usb_cdc_host_wait_rx(uint32_t generation, uint32_t timeout_ms);
bool usb_cdc_host_write(uint32_t generation, const uint8_t *, size_t, uint32_t timeout_ms);
bool usb_cdc_host_control(uint32_t generation, bool dtr, bool rts);
void usb_cdc_host_clear_rx(uint32_t generation);
// Internal ownership handoff shared with the Serial/JTAG service implementation.
bool suspend_usb_device_for_host(DeviceProjectionRegistry &, std::string &error);
bool resume_usb_device_after_host(DeviceProjectionRegistry &, std::string &error);
} // namespace lunaredu::lunaros
