#pragma once
#include "lunaros_provisioning_status.h"
namespace lunaredu::lunaros {
struct ProvisioningActivityHandle { uint64_t generation = 0; };
enum class ProvisioningActivityPhase { Preparing, Waiting, Succeeded, Failed, Cancelling, Cancelled };
struct ProvisioningActivitySnapshot {
  ProvisioningActivityHandle activity;
  ProvisioningActivityPhase phase = ProvisioningActivityPhase::Failed;
  ProvisioningSnapshot provisioning;
  std::string url, password, error;
};
// Trusted native caller identity, never supplied by request JSON. Only the
// initiating caller may observe local setup secrets or cancel its activity.
// Attaching an existing window grants observation, never ownership of its stop.
bool begin_provisioning_activity(const char *caller, ProvisioningActivityHandle &, std::string &);
bool read_provisioning_activity(const char *caller, ProvisioningActivityHandle, ProvisioningActivitySnapshot &, std::string &);
bool cancel_provisioning_activity(const char *caller, ProvisioningActivityHandle, std::string &);
}
