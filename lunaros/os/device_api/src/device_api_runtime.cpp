#include "device_api/device_api.h"
#include "device_api/device_api_context.h"
#include "platform/system.h"
namespace lunaredu::lunaros {
DeviceHostConnectionTracker::DeviceHostConnectionTracker(DeviceApiContext &owner) : owner_(owner) {
  if (!owner_.host_connection_) owner_.host_connection_ = this;
}
DeviceHostConnectionTracker::~DeviceHostConnectionTracker() {
  if (owner_.host_connection_ == this) owner_.host_connection_ = nullptr;
}
DeviceHostConnectionTracker *DeviceHostConnectionTracker::bound(const DeviceApiContext &owner) {
  return owner.host_connection_;
}
void report_device_host_connection(DeviceApiContext &device, DeviceHostTransport transport, DeviceHostConnectionState state) {
  auto *tracker = DeviceHostConnectionTracker::bound(device);
  if (!tracker) return;
  const auto slot = static_cast<size_t>(transport);
  if (slot >= tracker->transports_.size()) return;
  const auto now = system_monotonic_ms();
  std::lock_guard lock(tracker->mutex_);
  tracker->transports_[slot] = {state, now};
}
DeviceHostConnectionSnapshot device_host_connection_snapshot(const DeviceApiContext &device) {
  auto *tracker = DeviceHostConnectionTracker::bound(device);
  if (!tracker) return {};
  const auto now = system_monotonic_ms();
  std::lock_guard lock(tracker->mutex_);
  DeviceHostConnectionSnapshot result;
  auto rank=[](DeviceHostConnectionState state) {
    return state == DeviceHostConnectionState::Connected ? 3 : state == DeviceHostConnectionState::Lan ? 2 : state == DeviceHostConnectionState::Error ? 1 : 0;
  };
  for (auto entry : tracker->transports_) {
    if ((entry.state == DeviceHostConnectionState::Connected || entry.state == DeviceHostConnectionState::Lan) &&
        uint32_t(now - entry.last_activity_ms) > 12'000) entry.state = DeviceHostConnectionState::Disconnected;
    if (rank(entry.state) > rank(result.state)) result = entry;
  }
  return result;
}
}
