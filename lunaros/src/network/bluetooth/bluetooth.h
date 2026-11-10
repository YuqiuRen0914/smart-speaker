#pragma once
#include <cstddef>
#include <string>
namespace lunaredu::lunaros {
// Product/network owner task installs static profiles before first start.
// install registers GATT services before NimBLE starts. synchronized and host
// resets run on the NimBLE task; explicit stop invokes reset on the owner after
// the host has stopped. Callbacks must not start/stop the host.
// Profiles own GAP connections/advertising,
// while this runtime exclusively owns the controller and host lifecycle.
struct BluetoothProfile {
  const char *id;
  bool (*install)(void *, std::string &);
  void (*synchronized)(void *);
  void (*reset)(void *);
  void *context = nullptr;
  // Explicit owner shutdown, after reset. A host fault only invokes reset.
  void (*stopped)(void *) = nullptr;
};
bool register_bluetooth_profile(const BluetoothProfile &, std::string &error);
bool start_bluetooth(std::string &error);
bool stop_bluetooth(std::string &error);
bool bluetooth_synchronized();
} // namespace lunaredu::lunaros
