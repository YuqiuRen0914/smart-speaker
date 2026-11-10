#include "device_api/device_frame_endpoint.h"
#include "kernel/static_mutex.h"
#include "platform/system.h"
#include <array>
namespace lunaredu::lunaros {
namespace {
StaticMutex mutex;
KernelRuntime *owner = nullptr;
bool owned(const DeviceFrameContext &context) {
  return owner == &context.runtime && size_t(context.transport) < 2;
}
std::array<const DeviceFrameEndpoint *, 4> endpoints{};
struct Client {
  KernelRuntime *runtime = nullptr;
  uint32_t nonce = 0, expires = 0, retention_start = 0, retention_deadline = 0;
  bool retained = false, delivered = false;
};
std::array<Client, 2> clients;
uint32_t now() { return system_monotonic_ms(); }
bool before(uint32_t time, uint32_t deadline) {
  return static_cast<int32_t>(time - deadline) < 0;
}
} // namespace
DeviceFrameOwner::DeviceFrameOwner(KernelRuntime &runtime, bool acquire) : runtime_(runtime) {
  if (acquire) (void)bind();
}
bool DeviceFrameOwner::bind() {
  StaticLockGuard lock(mutex);
  if (bound_) return true;
  if (!owner) { owner = &runtime_; bound_ = true; }
  return bound_;
}
DeviceFrameOwner::~DeviceFrameOwner() { release(); }
void DeviceFrameOwner::release() {
  if (!bound_) return;
  StaticLockGuard lock(mutex);
  endpoints = {}; clients = {}; owner = nullptr;
  bound_ = false;
}
bool device_frame_supported(uint8_t type) {
  std::array<const DeviceFrameEndpoint *, 4> copy;
  {
    StaticLockGuard lock(mutex);
    copy = endpoints;
  }
  for (auto *e : copy)
    if (e && e->supports && e->supports(type))
      return true;
  return false;
}
bool install_device_frame_endpoint(KernelRuntime &runtime, const DeviceFrameEndpoint &e) {
  if (!e.handle)
    return false;
  StaticLockGuard lock(mutex);
  if (owner != &runtime) return false;
  for (auto *p : endpoints)
    if (p == &e)
      return true;
  for (auto &p : endpoints)
    if (!p) {
      p = &e;
      return true;
    }
  return false;
}
bool remove_device_frame_endpoint(KernelRuntime &runtime, const DeviceFrameEndpoint &e) {
  StaticLockGuard lock(mutex);
  if (owner != &runtime) return false;
  for (auto &p : endpoints)
    if (p == &e) {
      p = nullptr;
      return true;
    }
  return false;
}
bool dispatch_device_frame(const DeviceFrameContext &context,
                           const DeviceFrame &request,
                           DeviceFrame &response) {
  std::array<const DeviceFrameEndpoint *, 4> copy;
  {
    StaticLockGuard lock(mutex);
    if (!owned(context)) return false;
    copy = endpoints;
  }
  for (auto *e : copy)
    if (e && e->handle(context, request, response))
      return true;
  return false;
}
void disconnect_device_frame_transport(const DeviceFrameContext &context) {
  std::array<const DeviceFrameEndpoint *, 4> copy;
  {
    StaticLockGuard lock(mutex);
    if (!owned(context)) return;
    if(clients[size_t(context.transport)].runtime&&clients[size_t(context.transport)].runtime!=&context.runtime)
      return;
    clients[size_t(context.transport)] = {};
    copy = endpoints;
  }
  for (auto *e : copy)
    if (e && e->disconnected)
      e->disconnected(context);
}
bool set_device_frame_authority(const DeviceFrameContext &context,
                                uint32_t nonce, uint32_t expires) {
  StaticLockGuard lock(mutex);
  if (!owned(context) || !nonce) return false;
  clients[size_t(context.transport)] = {&context.runtime, nonce, expires};
  return true;
}
void revoke_device_frame_authority(const DeviceFrameContext &context) {
  StaticLockGuard lock(mutex);
  if (!owned(context)) return;
  auto &client = clients[size_t(context.transport)];
  if (client.runtime == &context.runtime)
    client = {};
}
bool device_frame_authorized(const DeviceFrameContext &context,
                             uint32_t nonce) {
  StaticLockGuard lock(mutex);
  if (!owned(context)) return false;
  const auto &client = clients[size_t(context.transport)];
  return nonce && client.runtime == &context.runtime && client.nonce == nonce &&
         before(now(), client.expires);
}
void retain_device_frame_reply(const DeviceFrameContext &context,
                               bool delivered) {
  StaticLockGuard lock(mutex);
  if (!owned(context)) return;
  auto &client = clients[size_t(context.transport)];
  const auto time = now();
  if (client.runtime != &context.runtime || !client.nonce ||
      !before(time, client.expires))
    return;
  if (!client.retained) {
    client.retained = true;
    client.retention_start = time;
    client.retention_deadline = time + 120000;
  }
  if (delivered && !client.delivered) {
    client.delivered = true;
    if (before(time + 5000, client.retention_deadline))
      client.retention_deadline = time + 5000;
  }
}
bool device_frame_channel_confirmed(const DeviceFrameContext &context) {
  StaticLockGuard lock(mutex);
  if (!owned(context)) return false;
  const auto &client=clients[size_t(context.transport)];
  return client.runtime==&context.runtime&&client.nonce&&before(now(),client.expires);
}
bool device_frame_reply_pending(KernelRuntime &runtime) {
  StaticLockGuard lock(mutex);
  if (owner != &runtime) return false;
  const auto time = now();
  for (const auto &client : clients)
    if (client.runtime == &runtime && client.retained &&
        before(time, client.expires) && before(time, client.retention_deadline))
      return true;
  return false;
}
} // namespace lunaredu::lunaros
