#pragma once
#include "kernel/service.h"
#include "display/screen_startup_health.h"

namespace lunaredu::lunaros { class KernelRuntime; }
namespace lunaredu::display {
// Product-owned borrowed ports and dependencies must outlive the service node.
struct ScreenServicePorts {
  void *context = nullptr;
  bool (*prepare)(void *, lunaros::KernelRuntime &, std::string &) = nullptr;
  const lunaros::ServiceDependency *dependencies = nullptr;
  size_t dependency_count = 0;
  bool (*retained)(void *) = nullptr; // Needed to prove no resources remain when no stop callback is supplied.
  bool (*stop)(void *,std::string &) = nullptr;
  bool (*ready)(void *) = nullptr;
  uint32_t (*completed_transfers)(void *) = nullptr;
};
// Connects resident Display preparation to the Core service lifecycle.
// This adapter does not allocate frames or create rendering workers itself.
class DisplayService {
 public:
  DisplayService(lunaros::KernelRuntime &, const char *id, ScreenServicePorts);
  DisplayService(const DisplayService &) = delete;
  DisplayService &operator=(const DisplayService &) = delete;
  lunaros::ServiceNode node();
  bool healthy();
 private:
  lunaros::ServiceStartResult start();
  bool stop(std::string &);
  lunaros::KernelRuntime &runtime_;
  const char *id_;
  ScreenServicePorts ports_;
  ScreenStartupHealth health_{true};
  bool attempted_ = false, stopped_ = false;
  lunaros::ServiceStartResult result_;
};
} // namespace lunaredu::display
