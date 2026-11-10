#include "network/network_service_internal.h"

#include "network/provisioning/network_provisioning.h"
#include "network/network_state_policy.h"

#include <utility>

namespace lunaredu::lunaros {
namespace {

NetworkService g_network_service;

} // namespace

const char *network_link_state_name(NetworkLinkState state) {
  switch (state) {
  case NetworkLinkState::Down:
    return "down";
  case NetworkLinkState::Connecting:
    return "connecting";
  case NetworkLinkState::Up:
    return "up";
  }
  return "down";
}

const char *network_internet_state_name(NetworkInternetState state) {
  switch (state) {
  case NetworkInternetState::Unknown:
    return "unknown";
  case NetworkInternetState::Unavailable:
    return "unavailable";
  case NetworkInternetState::Reachable:
    return "reachable";
  }
  return "unknown";
}

const char *network_provisioning_state_name(NetworkProvisioningState state) {
  switch (state) {
  case NetworkProvisioningState::Idle:
    return "idle";
  case NetworkProvisioningState::Active:
    return "active";
  case NetworkProvisioningState::Applying:
    return "applying";
  }
  return "idle";
}

const char *network_clock_state_name(NetworkClockState state) {
  switch (state) {
  case NetworkClockState::Unsynchronized:
    return "unsynchronized";
  case NetworkClockState::Synchronizing:
    return "synchronizing";
  case NetworkClockState::Valid:
    return "valid";
  }
  return "unsynchronized";
}

const char *network_device_link_state_name(NetworkDeviceLinkState state) {
  switch (state) {
  case NetworkDeviceLinkState::Disabled:
    return "disabled";
  case NetworkDeviceLinkState::Unbound:
    return "unbound";
  case NetworkDeviceLinkState::Connecting:
    return "connecting";
  case NetworkDeviceLinkState::Ready:
    return "ready";
  case NetworkDeviceLinkState::Degraded:
    return "degraded";
  }
  return "disabled";
}

void NetworkService::reset() {
  StaticLockGuard lock(mutex_);
  driver_ = {};
  policy_ = {};
  snapshot_ = {};
  provisioning_password_.clear();
}

bool NetworkService::bind(NetworkDriver driver, NetworkPolicy policy,
                          std::string &error) {
  StaticLockGuard lock(mutex_);
  error.clear();
  if (snapshot_.service_started) {
    error = "network_service_already_started";
    return false;
  }
  if (driver.start == nullptr || driver.stop == nullptr ||
      driver.poll == nullptr ||
      driver.observe == nullptr || policy.jobs.maximum_active_jobs == 0 ||
      policy.jobs.maximum_jobs_per_owner == 0) {
    error = "invalid_network_platform_driver";
    return false;
  }
  driver_ = driver;
  policy_ = policy;
  snapshot_.driver_bound = true;
  ++snapshot_.sequence;
  return true;
}

bool NetworkService::start(std::string &error) {
  NetworkDriver driver;
  NetworkPolicy policy;
  {
    StaticLockGuard lock(mutex_);
    error.clear();
    if (!snapshot_.driver_bound) {
      error = "network_platform_driver_not_bound";
      return false;
    }
    if (snapshot_.service_started) {
      return true;
    }
    driver = driver_;
    policy = policy_;
  }

  if (!driver.start(driver.context, policy, error)) {
    StaticLockGuard lock(mutex_);
    record_error_locked(error.empty() ? "network_platform_start_failed"
                                      : error.c_str());
    if (error.empty()) {
      error = "network_platform_start_failed";
    }
    return false;
  }


  const NetworkObservation observation = driver.observe(driver.context);
  {
    StaticLockGuard lock(mutex_);
    snapshot_.service_started = true;
    snapshot_.last_error.clear();
    ++snapshot_.sequence;
    apply_observation_locked(observation);
  }
  return true;
}

bool NetworkService::stop(std::string &error) {
  NetworkDriver driver;
  {
    StaticLockGuard lock(mutex_);
    error.clear();
    if (!snapshot_.service_started) {
      return true;
    }
    driver = driver_;
  }

  if (!driver.stop(driver.context, error)) {
    if (error.empty()) {
      error = "network_platform_stop_failed";
    }
    StaticLockGuard lock(mutex_);
    record_error_locked(error.c_str());
    return false;
  }

  StaticLockGuard lock(mutex_);
  snapshot_.service_started = false;
  snapshot_.platform_ready = false;
  snapshot_.credentials_available = false;
  snapshot_.link = NetworkLinkState::Down;
  snapshot_.internet = NetworkInternetState::Unknown;
  snapshot_.provisioning = NetworkProvisioningState::Idle;
  snapshot_.clock = NetworkClockState::Unsynchronized;
  snapshot_.device_link = NetworkDeviceLinkState::Disabled;
  snapshot_.local_ip.clear();
  snapshot_.target_ssid.clear();
  snapshot_.provisioning_ssid.clear();
  snapshot_.provisioning_ip.clear();
  provisioning_password_.clear();
  snapshot_.last_error.clear();
  ++snapshot_.sequence;
  return true;
}

void NetworkService::poll() {
  NetworkDriver driver;
  {
    StaticLockGuard lock(mutex_);
    if (!snapshot_.service_started) {
      return;
    }
    driver = driver_;
  }
  driver.poll(driver.context);
  tick_network_provisioning();
  const NetworkObservation observation = driver.observe(driver.context);
  NetworkProvisioningPolicy provisioning_policy;
  {
    StaticLockGuard lock(mutex_);
    apply_observation_locked(observation);
    provisioning_policy = policy_.provisioning;
  }

  const internal::NetworkProvisioningDecision decision =
      internal::decide_network_provisioning(
          provisioning_policy, observation,
          driver.control_provisioning != nullptr);
  if (decision.action == internal::NetworkProvisioningAction::Stop) {
    std::string error;
    (void)stop_provisioning_on_owner(error);
    return;
  }
  if (decision.action == internal::NetworkProvisioningAction::Start) {
    std::string error;
    (void)start_provisioning_on_owner(decision.mode,
                                      decision.suspend_station, error);
  }
}

bool NetworkService::start_provisioning_on_owner(NetworkProvisioningMode mode,
                                                 std::string &error) {
  return start_provisioning_on_owner(mode, true, error);
}

bool NetworkService::start_provisioning_on_owner(NetworkProvisioningMode mode,
                                                 bool suspend_station,
                                                 std::string &error) {
  NetworkDriver driver;
  {
    StaticLockGuard lock(mutex_);
    error.clear();
    if (!snapshot_.service_started || !policy_.provisioning.enabled) {
      error = "network_provisioning_unavailable";
      return false;
    }
    if (mode == NetworkProvisioningMode::Disabled) {
      error = "network_provisioning_mode_disabled";
      return false;
    }
    if (driver_.control_provisioning == nullptr) {
      error = "network_provisioning_not_supported";
      return false;
    }
    driver = driver_;
  }
  if (!driver.control_provisioning(driver.context, true, mode,
                                   suspend_station, error)) {
    if (error.empty()) {
      error = "network_provisioning_start_failed";
    }
    StaticLockGuard lock(mutex_);
    record_error_locked(error.c_str());
    return false;
  }
  const NetworkObservation observation = driver.observe(driver.context);
  StaticLockGuard lock(mutex_);
  apply_observation_locked(observation);
  return true;
}

bool NetworkService::stop_provisioning_on_owner(std::string &error) {
  NetworkDriver driver;
  {
    StaticLockGuard lock(mutex_);
    error.clear();
    if (!snapshot_.service_started ||
        driver_.control_provisioning == nullptr) {
      error = "network_provisioning_unavailable";
      return false;
    }
    driver = driver_;
  }
  if (!driver.control_provisioning(driver.context, false,
                                   NetworkProvisioningMode::SoftAp, false,
                                   error)) {
    if (error.empty()) {
      error = "network_provisioning_stop_failed";
    }
    StaticLockGuard lock(mutex_);
    record_error_locked(error.c_str());
    return false;
  }
  const NetworkObservation observation = driver.observe(driver.context);
  StaticLockGuard lock(mutex_);
  apply_observation_locked(observation);
  return true;
}

bool NetworkService::synchronize_clock_blocking(uint32_t timeout_ms,
                                                std::string &error,
                                                bool force) {
  StaticLockGuard sync_lock(clock_sync_mutex_);
  NetworkDriver driver;
  {
    StaticLockGuard lock(mutex_);
    error.clear();
    if (!snapshot_.service_started || driver_.synchronize_clock == nullptr) {
      error = "network_clock_sync_unavailable";
      return false;
    }
    snapshot_.clock = NetworkClockState::Synchronizing;
    ++snapshot_.sequence;
    driver = driver_;
  }
  if (!driver.synchronize_clock(driver.context, timeout_ms, force, error)) {
    if (error.empty()) {
      error = "network_clock_sync_failed";
    }
    StaticLockGuard lock(mutex_);
    snapshot_.clock = NetworkClockState::Unsynchronized;
    ++snapshot_.sequence;
    record_error_locked(error.c_str());
    return false;
  }
  report_clock(NetworkClockState::Valid);
  return true;
}

bool NetworkService::apply_saved_credential_on_owner(uint8_t index,
                                                     std::string &error) {
  NetworkDriver driver;
  {
    StaticLockGuard lock(mutex_);
    error.clear();
    if (!snapshot_.service_started ||
        driver_.apply_saved_credential == nullptr) {
      error = "network_saved_credential_unavailable";
      return false;
    }
    driver = driver_;
  }
  if (!driver.apply_saved_credential(driver.context, index, error)) {
    if (error.empty()) {
      error = "network_saved_credential_apply_failed";
    }
    StaticLockGuard lock(mutex_);
    record_error_locked(error.c_str());
    return false;
  }
  const NetworkObservation observation = driver.observe(driver.context);
  StaticLockGuard lock(mutex_);
  apply_observation_locked(observation);
  return true;
}

void NetworkService::report_internet(NetworkInternetState state,
                                     const char *error) {
  StaticLockGuard lock(mutex_);
  if (snapshot_.internet != state) {
    snapshot_.internet = state;
    ++snapshot_.sequence;
  }
  record_error_locked(error);
}

void NetworkService::report_clock(NetworkClockState state, const char *error) {
  StaticLockGuard lock(mutex_);
  if (snapshot_.clock != state) {
    snapshot_.clock = state;
    ++snapshot_.sequence;
  }
  record_error_locked(error);
}

void NetworkService::report_device_link(NetworkDeviceLinkState state,
                                        const char *error) {
  StaticLockGuard lock(mutex_);
  if (snapshot_.device_link != state) {
    snapshot_.device_link = state;
    ++snapshot_.sequence;
  }
  record_error_locked(error);
}

NetworkPolicy NetworkService::policy() const {
  StaticLockGuard lock(mutex_);
  return policy_;
}

NetworkSnapshot NetworkService::snapshot() const {
  StaticLockGuard lock(mutex_);
  return snapshot_;
}

NetworkFastSnapshot NetworkService::fast_snapshot() const {
  StaticLockGuard lock(mutex_);
  return {
      .sequence = snapshot_.sequence,
      .credentials_available = snapshot_.credentials_available,
      .link = snapshot_.link,
      .provisioning = snapshot_.provisioning,
  };
}

NetworkLinkState NetworkService::link_state() const {
  StaticLockGuard lock(mutex_);
  return snapshot_.link;
}

NetworkProvisioningState NetworkService::provisioning_state() const {
  StaticLockGuard lock(mutex_);
  return snapshot_.provisioning;
}

std::string NetworkService::local_provisioning_password() const {
  StaticLockGuard lock(mutex_);
  return provisioning_password_;
}

void NetworkService::apply_observation_locked(
    const NetworkObservation &observation) {
  provisioning_password_ = observation.provisioning_password;
  if (internal::apply_network_observation(snapshot_, observation)) {
    ++snapshot_.sequence;
  }
}

void NetworkService::record_error_locked(const char *error) {
  if (error == nullptr || error[0] == '\0') {
    return;
  }
  if (snapshot_.last_error != error) {
    snapshot_.last_error = error;
    ++snapshot_.sequence;
  }
}

NetworkService &network_service_internal() { return g_network_service; }

void reset_network_service_internal() { g_network_service.reset(); }

NetworkSnapshot network_snapshot() { return g_network_service.snapshot(); }

NetworkFastSnapshot network_fast_snapshot() {
  return g_network_service.fast_snapshot();
}

NetworkLinkState network_link_state() { return g_network_service.link_state(); }

NetworkProvisioningState network_provisioning_state() {
  return g_network_service.provisioning_state();
}

NetworkPolicy network_policy() { return g_network_service.policy(); }

void poll_network_on_owner() { g_network_service.poll(); }

bool synchronize_network_clock_blocking(uint32_t timeout_ms,
                                        std::string &error, bool force) {
  return g_network_service.synchronize_clock_blocking(timeout_ms, error,
                                                       force);
}

void report_network_internet(NetworkInternetState state, const char *error) {
  g_network_service.report_internet(state, error);
}

void report_network_clock(NetworkClockState state, const char *error) {
  g_network_service.report_clock(state, error);
}

void report_network_device_link(NetworkDeviceLinkState state,
                                const char *error) {
  g_network_service.report_device_link(state, error);
}

std::string network_local_provisioning_password() {
  return g_network_service.local_provisioning_password();
}

} // namespace lunaredu::lunaros
