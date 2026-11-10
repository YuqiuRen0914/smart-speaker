#pragma once

#include "kernel/static_mutex.h"
#include "lunaredu_lunaros/network/network.h"

namespace lunaredu::lunaros {

// Low-level binding used only by the complete runtime lifecycle.
bool configure_network_service(const NetworkPolicy &policy,
                               const char *timezone, std::string &error);

struct NetworkObservation {
  bool ready = false;
  bool credentials_available = false;
  bool station_suspended = false;
  NetworkLinkState link = NetworkLinkState::Down;
  int32_t rssi = 0;
  NetworkProvisioningState provisioning = NetworkProvisioningState::Idle;
  NetworkClockState clock = NetworkClockState::Unsynchronized;
  uint32_t monotonic_ms = 0;
  uint32_t connection_failure_started_at_ms = 0;
  uint32_t reconnect_attempts = 0;
  std::string local_ip;
  std::string target_ssid;
  std::string provisioning_ssid;
  std::string provisioning_ip;
  std::string provisioning_password;
};

using NetworkDriverStart = bool (*)(void *context,
                                    const NetworkPolicy &policy,
                                    std::string &error);
using NetworkDriverStop = bool (*)(void *context, std::string &error);
using NetworkDriverPoll = void (*)(void *context);
using NetworkObservationReader = NetworkObservation (*)(void *context);
using NetworkProvisioningControl = bool (*)(
    void *context, bool start, NetworkProvisioningMode mode,
    bool suspend_station, std::string &error);
using NetworkClockSync = bool (*)(void *context, uint32_t timeout_ms,
                                 bool force, std::string &error);
using NetworkSavedCredentialApply = bool (*)(void *context, uint8_t index,
                                             std::string &error);

struct NetworkDriver {
  NetworkDriverStart start = nullptr;
  NetworkDriverStop stop = nullptr;
  NetworkDriverPoll poll = nullptr;
  NetworkObservationReader observe = nullptr;
  NetworkProvisioningControl control_provisioning = nullptr;
  NetworkClockSync synchronize_clock = nullptr;
  NetworkSavedCredentialApply apply_saved_credential = nullptr;
  void *context = nullptr;
};

class NetworkService {
public:
  void reset();
  bool bind(NetworkDriver driver, NetworkPolicy policy, std::string &error);
  bool start(std::string &error);
  bool stop(std::string &error);
  void poll();
  bool start_provisioning_on_owner(NetworkProvisioningMode mode,
                                   std::string &error);
  bool start_provisioning_on_owner(NetworkProvisioningMode mode,
                                   bool suspend_station,
                                   std::string &error);
  bool stop_provisioning_on_owner(std::string &error);
  bool apply_saved_credential_on_owner(uint8_t index, std::string &error);
  bool synchronize_clock_blocking(uint32_t timeout_ms, std::string &error,
                                  bool force = false);
  void report_internet(NetworkInternetState state, const char *error = nullptr);
  void report_clock(NetworkClockState state, const char *error = nullptr);
  void report_device_link(NetworkDeviceLinkState state,
                          const char *error = nullptr);
  NetworkPolicy policy() const;
  NetworkSnapshot snapshot() const;
  NetworkFastSnapshot fast_snapshot() const;
  NetworkLinkState link_state() const;
  NetworkProvisioningState provisioning_state() const;
  std::string local_provisioning_password() const;

private:
  void apply_observation_locked(const NetworkObservation &observation);
  void record_error_locked(const char *error);

  mutable StaticMutex mutex_;
  StaticMutex clock_sync_mutex_;
  NetworkDriver driver_{};
  NetworkPolicy policy_{};
  NetworkSnapshot snapshot_{};
  std::string provisioning_password_;
};

NetworkService &network_service_internal();
void reset_network_service_internal();

} // namespace lunaredu::lunaros
