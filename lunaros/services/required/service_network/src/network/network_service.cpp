#include "lunaros_network.h"
#include "lunaros_network_control.h"
#include "lunaros_network_policy.h"
#include "lunaros_network_jobs.h"
#include "lunaros_network_local_access.h"
#include "kernel/runtime.h"
#include "lunaros_network_lifecycle.h"
#include "network/network_service_internal.h"
#include "lunaros_network_work_timing.h"

#include "internal/lunaros_network_local_access.h"
#include "network/network_state_policy.h"

#include <utility>
#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#if CONFIG_MBEDTLS_ECP_RESTARTABLE
#include "psa/crypto.h"
#endif
#endif

namespace lunaredu::lunaros {
KernelRuntime *NetworkService::owner_runtime() const {
  StaticLockGuard lock(mutex_);return driver_.runtime;
}
namespace {

NetworkService g_network_service;

} // namespace

void NetworkService::reset() {
  StaticLockGuard lock(mutex_);
  if (retirement_pending_) return; // Reset is not an SDK retirement acknowledgement.
  driver_ = {};
  policy_ = {};
  snapshot_ = {};
  observation_ = {};
}

bool NetworkService::owned_by(const KernelRuntime &runtime) const {
  StaticLockGuard lock(mutex_);
  return driver_.runtime == &runtime;
}

bool NetworkService::bind(NetworkDriver driver, NetworkPolicy policy,
                          std::string &error) {
  StaticLockGuard lock(mutex_);
  error.clear();
  if (retirement_pending_) { error = "network_platform_retirement_pending"; return false; }
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
#if defined(ESP_PLATFORM) && CONFIG_MBEDTLS_ECP_RESTARTABLE
  // Set once before the driver starts any TLS clients. Resumable handshake
  // work uses this operation budget; certificate validation stays complete.
  static const bool crypto_budget_initialized = [] {
    psa_interruptible_set_max_ops(128);
    return true;
  }();
  (void)crypto_budget_initialized;
#endif
  NetworkDriver driver;
  NetworkPolicy policy;
  bool retire_first;
  {
    StaticLockGuard lock(mutex_);
    error.clear();
    if (!snapshot_.driver_bound) {
      error = "network_platform_driver_not_bound";
      return false;
    }
    if (snapshot_.service_started) {
      if (retirement_pending_) { error = "network_platform_retirement_pending"; return false; }
      return true;
    }
    driver = driver_;
    policy = policy_;
    retire_first = retirement_pending_;
  }
  if (retire_first && !stop(error)) return false;

  if (!driver.start(driver.context, policy, error)) {
    if (error.empty()) {
      error = "network_platform_start_failed";
    }
    std::string cleanup_error;
    const bool retired = driver.stop(driver.context, cleanup_error);
    if (!retired) error += ":cleanup_pending:" + cleanup_error;
    StaticLockGuard lock(mutex_);
    retirement_pending_ = !retired;
    record_error_locked(error.c_str());
    return false;
  }


  driver.observe(driver.context, observation_);
  const auto &observation = observation_;
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
    if (!snapshot_.service_started && !retirement_pending_) {
      return true;
    }
    if (!retirement_pending_) ++snapshot_.sequence;
    retirement_pending_ = true;
    driver = driver_;
  }

  if (!driver.stop(driver.context, error)) {
    if (error.empty()) {
      error = "network_platform_stop_failed";
    }
    StaticLockGuard lock(mutex_);
    retirement_pending_ = true;
    record_error_locked(error.c_str());
    return false;
  }

  StaticLockGuard lock(mutex_);
  retirement_pending_ = false;
  snapshot_.service_started = false;
  snapshot_.platform_ready = false;
  snapshot_.credentials_available = false;
  snapshot_.link = NetworkLinkState::Down;
  snapshot_.local_access = NetworkLocalAccessState::Idle;
  snapshot_.bluetooth_enabled = false;
  snapshot_.bluetooth_connected = false;
  snapshot_.clock = NetworkClockState::Unsynchronized;
  snapshot_.local_ip.clear();
  snapshot_.target_ssid.clear();
  snapshot_.local_access_ssid.clear();
  snapshot_.local_access_ip.clear();
  snapshot_.last_error.clear();
  ++snapshot_.sequence;
  return true;
}

void NetworkService::poll() {
  NetworkDriver driver;
  bool retire_failed_start = false;
  {
    StaticLockGuard lock(mutex_);
    if (!snapshot_.service_started) {
      if (!retirement_pending_) return;
      retire_failed_start = true;
    }
    driver = driver_;
  }
  if (retire_failed_start) { std::string error; (void)stop(error); return; }
  driver.poll(driver.context);
  network_work(NetworkWorkStage::Access,[]{tick_network_local_access();});
  network_work(NetworkWorkStage::Observation,[&]{driver.observe(driver.context, observation_);});
  StaticLockGuard lock(mutex_);apply_observation_locked(observation_);
}
void NetworkService::begin_retirement() {
  StaticLockGuard lock(mutex_);
  if (!retirement_pending_) ++snapshot_.sequence;
  retirement_pending_ = true;
}
bool NetworkService::retirement_pending() const {
  StaticLockGuard lock(mutex_);
  return retirement_pending_;
}
bool NetworkService::open_local_access_on_owner(const NetworkLocalAccessConfig &config,NetworkLocalAccessLease &lease,std::string &error) {
  NetworkDriver driver;
  {StaticLockGuard lock(mutex_);if(retirement_pending_){error="network_platform_retirement_pending";return false;}if(!snapshot_.service_started||!driver_.control_local_access){error="network_local_access_unavailable";return false;}driver=driver_;}
  const bool opened=driver.control_local_access(driver.context,true,config,lease,error);
  driver.observe(driver.context,observation_);StaticLockGuard lock(mutex_);apply_observation_locked(observation_);return opened;
}
bool NetworkService::close_local_access_on_owner(NetworkLocalAccessLease lease,std::string &error) {
  NetworkDriver driver;
  {StaticLockGuard lock(mutex_);if(!snapshot_.service_started||!driver_.control_local_access){error="network_local_access_unavailable";return false;}driver=driver_;}
  const bool stopped=driver.control_local_access(driver.context,false,{},lease,error);
  driver.observe(driver.context,observation_);StaticLockGuard lock(mutex_);apply_observation_locked(observation_);return stopped;
}

bool NetworkService::synchronize_clock_blocking(uint32_t timeout_ms,
                                                std::string &error,
                                                bool force) {
  StaticLockGuard sync_lock(clock_sync_mutex_);
  NetworkDriver driver;
  {
    StaticLockGuard lock(mutex_);
    error.clear();
    if (retirement_pending_) { error="network_platform_retirement_pending"; return false; }
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
    if (retirement_pending_) { error="network_platform_retirement_pending"; return false; }
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
  driver.observe(driver.context, observation_);
  const auto &observation = observation_;
  StaticLockGuard lock(mutex_);
  apply_observation_locked(observation);
  return true;
}

void NetworkService::report_clock(NetworkClockState state, const char *error) {
  StaticLockGuard lock(mutex_);
  if (snapshot_.clock != state) {
    snapshot_.clock = state;
    ++snapshot_.sequence;
  }
  record_error_locked(error);
}

NetworkJobPolicy NetworkService::job_policy() const {
  StaticLockGuard lock(mutex_);
  return policy_.jobs;
}

NetworkSnapshot NetworkService::snapshot() const {
  NetworkSnapshot snapshot;
  snapshot_into(snapshot);
  return snapshot;
}

void NetworkService::snapshot_into(NetworkSnapshot &snapshot) const {
  StaticLockGuard lock(mutex_);
  snapshot = snapshot_;
  snapshot.fixed_wifi_credentials = policy_.force_initial_wifi_credentials;
}

NetworkFastSnapshot NetworkService::fast_snapshot() const {
  StaticLockGuard lock(mutex_);
  return {
      .retirement_pending = retirement_pending_,
      .clock = snapshot_.clock,
      .fixed_wifi_credentials = policy_.force_initial_wifi_credentials,
      .channel = static_cast<uint8_t>(snapshot_.channel),
      .sequence = snapshot_.sequence,
      .service_started = snapshot_.service_started,
      .station_enabled = policy_.station_enabled,
      .credentials_available = snapshot_.credentials_available,
      .link = snapshot_.link,
      .local_access = snapshot_.local_access,
      .platform_ready = snapshot_.platform_ready,
      .local_access_has_address = !snapshot_.local_access_ip.empty(),
      .station_has_address = !snapshot_.local_ip.empty(),
      .local_access_surface_active = snapshot_.local_access != NetworkLocalAccessState::Idle &&
          (!snapshot_.local_access_ssid.empty() || !snapshot_.local_access_ip.empty()),
      .bluetooth_available = snapshot_.bluetooth_available,
      .bluetooth_enabled = snapshot_.bluetooth_enabled,
      .bluetooth_connected = snapshot_.bluetooth_connected,
  };
}

void NetworkService::apply_observation_locked(
    const NetworkObservation &observation) {
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

bool network_runtime_owned_by(const KernelRuntime &runtime) {
  return g_network_service.owned_by(runtime);
}

bool require_network_runtime(const KernelRuntime &runtime, std::string &error) {
  if (!network_runtime_owned_by(runtime)) {
    error = "network_runtime_owner_mismatch";
    return false;
  }
  error.clear();
  return true;
}

void reset_network_service_internal() { g_network_service.reset(); }

NetworkSnapshot network_snapshot() { return g_network_service.snapshot(); }
void network_snapshot_into(NetworkSnapshot &snapshot) { g_network_service.snapshot_into(snapshot); }

NetworkFastSnapshot network_fast_snapshot() {
  return g_network_service.fast_snapshot();
}

void poll_network_on_owner() { g_network_service.poll(); }

bool synchronize_network_clock_blocking(uint32_t timeout_ms,
                                        std::string &error, bool force) {
  return g_network_service.synchronize_clock_blocking(timeout_ms, error,
                                                       force);
}

} // namespace lunaredu::lunaros
