#pragma once
#include "kernel/service_executor.h"
#include <cstdint>
#include <span>
#include <string>
namespace lunaredu::app {class AppController;}
namespace lunaredu::lunaros {
struct DeviceContext;
struct UpdateMaintenanceParticipant {
  const char *id=nullptr;
  const char *executor=nullptr;
  void *context=nullptr;
  // Runs only on this participant's executor. False retains a partially
  // retired owner and is retried. A release must not replay old outputs.
  bool (*step)(void *,bool entering,uint32_t epoch,std::string &error)=nullptr;
  // Optional, bounded owner-local cancellation before foreground retirement.
  // This revokes old intent without claiming a physical pause acknowledgement.
  void (*revoke)(void *,uint32_t epoch)=nullptr;
};
struct UpdateMaintenanceDefinition {
  std::span<const UpdateMaintenanceParticipant> participants{};
  void *context=nullptr;
  // Atomic/locked revocation hook. Called once per admission/release, never
  // used as evidence of physical retirement. Must not wait on another owner.
  void (*admission)(void *,bool blocked,uint32_t epoch)=nullptr;
  uint32_t timeout_ms=5000;
  bool retire_background=true;
};
// Configure during boot, before executor tasks or external requests are admitted.
// Unselected products allocate no update coordinator.
bool configure_update_maintenance(DeviceContext &,UpdateMaintenanceDefinition,std::string &error);
void poll_update_maintenance(DeviceContext &,const char *executor);
bool update_maintenance_active(const DeviceContext &);
// Admission may reopen while an asynchronous foreground restore is pending.
bool update_maintenance_blocked(const DeviceContext &);
bool update_maintenance_quiesced(const DeviceContext &);
uint32_t update_maintenance_epoch(const DeviceContext &);
}
