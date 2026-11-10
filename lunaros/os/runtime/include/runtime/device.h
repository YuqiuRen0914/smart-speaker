#pragma once

#include <memory>
#include "runtime/device_options.h"

namespace lunaredu::lunaros {
namespace apps { class ApplicationsService; }
class KernelRuntime;
struct DeviceContext;
class DeviceProjectionRegistry;
enum class DevicePollScope { Complete, SystemOnly };
enum class StartupFailureOwner { Device, BootCoordinator };

// One physical device, one boot lifetime. Keep this object alive until reboot
// (normally static). A failed start must still be polled for recovery/rollback.
// start is one-shot: retrying a partially running graph is never implicit.
class Device {
public:
  Device();
  ~Device();
  Device(const Device &) = delete;
  Device &operator=(const Device &) = delete;
  bool start(const DeviceOptions &options,std::string &error,StartupFailureOwner failure_owner=StartupFailureOwner::Device);
  void poll(bool application_healthy = true, DevicePollScope scope = DevicePollScope::Complete);
  // Advanced loop staging: the standard product loop keeps module work after
  // radio delivery and before product work. Ordinary callers use poll().
  void poll_modules();
  // Opt in only after complete product startup. Failed boot keeps maintenance
  // polling. Checkpoints run serially on the same foreground executor.
  void use_foreground_io_checkpoints();
  void poll_foreground_io();
  [[noreturn]] void run(const DeviceOptions &options = {});
  apps::ApplicationsService &applications_service();
  DeviceContext &context();
  KernelRuntime &runtime(); // Advanced service/application composition only.
  DeviceProjectionRegistry &projections(); // Advanced protocol declarations only.
private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace lunaredu::lunaros
