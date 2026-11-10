#pragma once
#include <cstdint>
#include <string>
namespace lunaredu::lunaros { struct ApplicationContext; }
namespace lunaredu::lunaros::apps {
struct ApplicationCleanupStatus {
  bool pending = true, running = false;
  uint32_t attempts = 0, retiring_owners = 0;
  std::string detail = "application_cleanup_pending";
};
// Borrowed observation/control port. Composition owns the provider's lifetime;
// Core stores no cleanup state, retry flag, clock or wake callback.
struct ApplicationMaintenancePorts {
  void *context = nullptr;
  ApplicationCleanupStatus (*status)(void *) = nullptr;
  bool (*retry)(void *) = nullptr;
};
// Composition supplies one boot-lived I/O lane. Disable closes new work and
// wakes its waiter; retired acknowledges managed-wrapper release, not merely
// callback return. Service stop retains borrowed context until that receipt.
struct ApplicationMaintenanceWorkerPorts {
  void *context=nullptr;
  void (*enable)(void *,bool)=nullptr;
  bool (*retired)(void *)=nullptr;
};
const ApplicationMaintenancePorts &application_maintenance(const ApplicationContext &);
}
