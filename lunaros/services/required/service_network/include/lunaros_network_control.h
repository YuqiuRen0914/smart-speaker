#pragma once
#include <cstdint>
#include <string>
namespace lunaredu::lunaros {
class KernelRuntime;
struct NetworkBluetoothControlSnapshot {
  bool enabled = false; // Actual host running, not the requested switch value.
  bool stop_pending = false; // Includes profile, host and controller retirement.
  std::string error; // Failure survives SDK cooldown; cleared on completion.
};
NetworkBluetoothControlSnapshot network_bluetooth_control_snapshot();
bool network_bluetooth_stop_pending(); // Scalar observation for frequent UI polling.
// True accepts the request. Off may retire asynchronously; completion requires
// !snapshot.enabled && !snapshot.stop_pending. Owner stop returns true only on
// complete resource retirement. Commands retain Runtime ownership checks.
bool request_network_set_bluetooth(KernelRuntime &, bool enabled, std::string &error);
bool request_network_apply_saved_credential(KernelRuntime &, uint8_t index, std::string &error);
// Blocking clock I/O; callers retain their existing background execution.
bool synchronize_network_clock_blocking(uint32_t timeout_ms, std::string &error,
    bool force = false);
} // namespace lunaredu::lunaros
