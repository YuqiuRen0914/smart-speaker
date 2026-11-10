#pragma once
#include "lunaros_device_protocol.h"
#include <cstdint>
namespace lunaredu::lunaros {
class KernelRuntime;
// Exactly one boot-lifetime physical frame owner. Failed bindings have no side
// effects. Stop transport readers and remove endpoints before destruction.
class DeviceFrameOwner {
public:
  explicit DeviceFrameOwner(KernelRuntime &, bool acquire = true);
  ~DeviceFrameOwner();
  bool bind();
  // Composition-only: stop readers before release. Early startup failures may
  // release before any transport is admitted; failed retirement retains owner.
  void release();
  DeviceFrameOwner(const DeviceFrameOwner &) = delete;
  DeviceFrameOwner &operator=(const DeviceFrameOwner &) = delete;
  bool owns_runtime() const { return bound_; }
private:
  KernelRuntime &runtime_;
  bool bound_ = false;
};
enum class DeviceFrameTransport : uint8_t { Usb, Ble };
struct DeviceFrameContext {
  KernelRuntime &runtime;
  DeviceFrameTransport transport;
};
// Function owners register handlers. Transports never own their business state.
struct DeviceFrameEndpoint {
  bool (*handle)(const DeviceFrameContext &, const DeviceFrame &,
                 DeviceFrame &);
  void (*disconnected)(const DeviceFrameContext &) = nullptr;
  bool (*supports)(uint8_t type) = nullptr;
};
bool device_frame_supported(uint8_t type);
// Endpoint descriptors are immutable and outlive in-flight dispatch callbacks.
// Owner serializes registration/withdrawal with transport retirement.
bool install_device_frame_endpoint(KernelRuntime &, const DeviceFrameEndpoint &);
bool remove_device_frame_endpoint(KernelRuntime &, const DeviceFrameEndpoint &);
bool dispatch_device_frame(const DeviceFrameContext &,
                           const DeviceFrame &, DeviceFrame &);
void disconnect_device_frame_transport(const DeviceFrameContext &);
// A confirmed client can be consumed by another function without importing
// the function that issued its proof. Authority is bound to runtime and
// channel.
bool set_device_frame_authority(const DeviceFrameContext &, uint32_t nonce,
                                uint32_t expires_at_ms);
void revoke_device_frame_authority(const DeviceFrameContext &);
bool device_frame_authorized(const DeviceFrameContext &, uint32_t nonce);
// Point-to-point transports may use their already confirmed channel. This does
// not authorize another runtime or transport and is cleared on disconnect.
bool device_frame_channel_confirmed(const DeviceFrameContext &);
// Bounded reply handoff keeps a temporary channel alive; no account state here.
void retain_device_frame_reply(const DeviceFrameContext &, bool delivered);
bool device_frame_reply_pending(KernelRuntime &);
} // namespace lunaredu::lunaros
