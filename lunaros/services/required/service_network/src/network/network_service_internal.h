#pragma once
#include "lunaros_network_jobs.h"

#include "kernel/static_mutex.h"
#include "lunaros_network.h"
#include "lunaros_network_policy.h"
#include "lunaros_network_local_access.h"

namespace lunaredu::lunaros {

// Low-level binding used only by the complete runtime lifecycle.
bool configure_network_service(KernelRuntime &runtime, const NetworkPolicy &policy,
                               const char *timezone, std::string &error);

// Driver observations and published physical facts share one value contract.
// Lifecycle, revision, errors and policy remain owned by NetworkService.
using NetworkObservation = NetworkPhysicalState;

using NetworkDriverStart = bool (*)(void *context,
                                    const NetworkPolicy &policy,
                                    std::string &error);
using NetworkDriverStop = bool (*)(void *context, std::string &error);
using NetworkDriverPoll = void (*)(void *context);
using NetworkObservationReader = void (*)(void *context, NetworkObservation &output);
using NetworkLocalAccessControl = bool (*)(void *,bool,const NetworkLocalAccessConfig &,NetworkLocalAccessLease &,std::string &);
using NetworkClockSync = bool (*)(void *context, uint32_t timeout_ms,
                                 bool force, std::string &error);
using NetworkSavedCredentialApply = bool (*)(void *context, uint8_t index,
                                             std::string &error);

struct NetworkDriver {
  NetworkDriverStart start = nullptr;
  NetworkDriverStop stop = nullptr;
  NetworkDriverPoll poll = nullptr;
  NetworkObservationReader observe = nullptr;
  NetworkLocalAccessControl control_local_access = nullptr;
  NetworkClockSync synchronize_clock = nullptr;
  NetworkSavedCredentialApply apply_saved_credential = nullptr;
  void *context = nullptr;
  // The physical radio is exclusive to this kernel composition. Test drivers
  // may omit it when exercising a standalone NetworkService.
  KernelRuntime *runtime = nullptr;
};

class NetworkService {
public:
  void reset();
  bool owned_by(const KernelRuntime &runtime) const;
  KernelRuntime *owner_runtime() const;
  bool bind(NetworkDriver driver, NetworkPolicy policy, std::string &error);
  bool start(std::string &error);
  bool stop(std::string &error);
  bool retirement_pending() const;
  void begin_retirement();
  void poll();
  bool open_local_access_on_owner(const NetworkLocalAccessConfig &,NetworkLocalAccessLease &,std::string &);
  bool close_local_access_on_owner(NetworkLocalAccessLease,std::string &);
  bool apply_saved_credential_on_owner(uint8_t index, std::string &error);
  bool synchronize_clock_blocking(uint32_t timeout_ms, std::string &error,
                                  bool force = false);
  void report_clock(NetworkClockState state, const char *error = nullptr);
  NetworkJobPolicy job_policy() const;
  NetworkSnapshot snapshot() const;
  void snapshot_into(NetworkSnapshot &) const;
  NetworkFastSnapshot fast_snapshot() const;

private:
  void apply_observation_locked(const NetworkObservation &observation);
  void record_error_locked(const char *error);

  mutable StaticMutex mutex_;
  StaticMutex clock_sync_mutex_;
  NetworkDriver driver_{};
  NetworkPolicy policy_{};
  NetworkSnapshot snapshot_{};
  NetworkObservation observation_; // Network owner only; keeps text capacity between polls.
  bool retirement_pending_ = false; // Includes failed startup with acquired SDK resources.
};

// Shared admission rule for commands, scan, Wi-Fi configuration and monitoring.
inline bool admit_network_work(std::string &error) {
  if (network_fast_snapshot().retirement_pending) {
    error = "network_platform_retirement_pending";
    return false;
  }
  return true;
}

NetworkService &network_service_internal();
void reset_network_service_internal();
bool require_network_runtime(const KernelRuntime &runtime, std::string &error);

} // namespace lunaredu::lunaros
