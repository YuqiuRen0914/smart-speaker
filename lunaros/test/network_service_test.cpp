#include <iostream>
#include <string>

#include "network/network_service_internal.h"

namespace {

int g_failures = 0;

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::cerr << "CHECK failed line=" << __LINE__ << ": " #condition       \
                << std::endl;                                                  \
      ++g_failures;                                                            \
    }                                                                          \
  } while (false)

struct FakePlatform {
  bool started = false;
  bool stopped = false;
  bool connect_on_poll = true;
  size_t polls = 0;
  size_t provisioning_controls = 0;
  bool provisioning_suspended_station = false;
  uint8_t applied_credential = 0xff;
  bool clock_force = false;
  lunaredu::lunaros::NetworkObservation observation;
};

bool start(void *context, const lunaredu::lunaros::NetworkPolicy &policy,
           std::string &error) {
  auto *platform = static_cast<FakePlatform *>(context);
  if (!policy.provisioning.enabled) {
    error = "provisioning_required_by_test";
    return false;
  }
  platform->started = true;
  platform->observation.ready = true;
  platform->observation.credentials_available = true;
  platform->observation.link = lunaredu::lunaros::NetworkLinkState::Connecting;
  platform->observation.provisioning =
      lunaredu::lunaros::NetworkProvisioningState::Active;
  platform->observation.provisioning_ssid = "LunarEdu-Setup";
  platform->observation.provisioning_password = "device-local-secret";
  return true;
}

bool stop(void *context, std::string &error) {
  auto *platform = static_cast<FakePlatform *>(context);
  platform->started = false;
  platform->stopped = true;
  error.clear();
  return true;
}

void poll(void *context) {
  auto *platform = static_cast<FakePlatform *>(context);
  ++platform->polls;
  if (platform->connect_on_poll) {
    platform->observation.link = lunaredu::lunaros::NetworkLinkState::Up;
    platform->observation.local_ip = "192.168.1.20";
  }
}

lunaredu::lunaros::NetworkObservation observe(void *context) {
  return static_cast<FakePlatform *>(context)->observation;
}

bool control_provisioning(
    void *context, bool start,
    lunaredu::lunaros::NetworkProvisioningMode mode, bool suspend_station,
    std::string &error) {
  auto *platform = static_cast<FakePlatform *>(context);
  ++platform->provisioning_controls;
  platform->provisioning_suspended_station = suspend_station;
  platform->observation.provisioning =
      start ? lunaredu::lunaros::NetworkProvisioningState::Active
            : lunaredu::lunaros::NetworkProvisioningState::Idle;
  platform->observation.provisioning_ssid =
      start && mode == lunaredu::lunaros::NetworkProvisioningMode::SoftAp
          ? "LunarEdu-Setup"
          : "";
  error.clear();
  return true;
}

bool apply_saved_credential(void *context, uint8_t index, std::string &error) {
  auto *platform = static_cast<FakePlatform *>(context);
  platform->applied_credential = index;
  platform->observation.provisioning =
      lunaredu::lunaros::NetworkProvisioningState::Applying;
  error.clear();
  return true;
}

bool synchronize_clock(void *context, uint32_t, bool force,
                       std::string &error) {
  auto *platform = static_cast<FakePlatform *>(context);
  platform->clock_force = force;
  error.clear();
  return true;
}

void test_network_service_owns_orthogonal_state() {
  lunaredu::lunaros::NetworkService service;
  service.reset();
  std::string error;
  CHECK(!service.start(error));
  CHECK(error == "network_platform_driver_not_bound");

  FakePlatform platform;
  lunaredu::lunaros::NetworkPolicy policy;
  policy.jobs.maximum_active_jobs = 8;
  lunaredu::lunaros::NetworkDriver driver;
  driver.start = start;
  driver.stop = stop;
  driver.poll = poll;
  driver.observe = observe;
  driver.control_provisioning = control_provisioning;
  driver.synchronize_clock = synchronize_clock;
  driver.apply_saved_credential = apply_saved_credential;
  driver.context = &platform;
  CHECK(service.bind(driver, policy, error));
  CHECK(service.start(error));
  CHECK(platform.started);

  auto snapshot = service.snapshot();
  CHECK(snapshot.driver_bound);
  CHECK(snapshot.service_started);
  CHECK(snapshot.link == lunaredu::lunaros::NetworkLinkState::Connecting);
  CHECK(snapshot.internet ==
        lunaredu::lunaros::NetworkInternetState::Unknown);
  CHECK(service.local_provisioning_password() == "device-local-secret");

  service.poll();
  service.report_internet(
      lunaredu::lunaros::NetworkInternetState::Reachable);
  service.report_clock(lunaredu::lunaros::NetworkClockState::Valid);
  service.report_device_link(
      lunaredu::lunaros::NetworkDeviceLinkState::Ready);
  snapshot = service.snapshot();
  CHECK(platform.polls == 1);
  CHECK(snapshot.link == lunaredu::lunaros::NetworkLinkState::Up);
  CHECK(snapshot.internet ==
        lunaredu::lunaros::NetworkInternetState::Reachable);
  CHECK(snapshot.clock == lunaredu::lunaros::NetworkClockState::Valid);
  CHECK(snapshot.device_link ==
        lunaredu::lunaros::NetworkDeviceLinkState::Ready);
  CHECK(snapshot.local_ip == "192.168.1.20");
  CHECK(snapshot.sequence >= 5);
  CHECK(platform.provisioning_controls == 1);
  CHECK(snapshot.provisioning ==
        lunaredu::lunaros::NetworkProvisioningState::Idle);

  CHECK(service.synchronize_clock_blocking(1000, error, true));
  CHECK(platform.clock_force);
  CHECK(service.snapshot().clock ==
        lunaredu::lunaros::NetworkClockState::Valid);

  CHECK(service.start_provisioning_on_owner(
      lunaredu::lunaros::NetworkProvisioningMode::SoftAp, false, error));
  snapshot = service.snapshot();
  CHECK(platform.provisioning_controls == 2);
  CHECK(!platform.provisioning_suspended_station);
  CHECK(snapshot.provisioning ==
        lunaredu::lunaros::NetworkProvisioningState::Active);
  CHECK(snapshot.provisioning_ssid == "LunarEdu-Setup");

  CHECK(service.apply_saved_credential_on_owner(2, error));
  snapshot = service.snapshot();
  CHECK(platform.applied_credential == 2);
  CHECK(snapshot.provisioning ==
        lunaredu::lunaros::NetworkProvisioningState::Applying);

  CHECK(service.stop_provisioning_on_owner(error));
  snapshot = service.snapshot();
  CHECK(platform.provisioning_controls == 3);
  CHECK(snapshot.provisioning ==
        lunaredu::lunaros::NetworkProvisioningState::Idle);

  platform.connect_on_poll = false;
  platform.observation.link = lunaredu::lunaros::NetworkLinkState::Down;
  platform.observation.local_ip.clear();
  service.poll();
  service.report_internet(
      lunaredu::lunaros::NetworkInternetState::Unavailable);
  CHECK(service.snapshot().internet ==
        lunaredu::lunaros::NetworkInternetState::Unavailable);
  service.poll();
  CHECK(service.snapshot().internet ==
        lunaredu::lunaros::NetworkInternetState::Unknown);

  CHECK(service.stop(error));
  CHECK(platform.stopped);
  snapshot = service.snapshot();
  CHECK(snapshot.driver_bound);
  CHECK(!snapshot.service_started);
  CHECK(!snapshot.platform_ready);
  CHECK(snapshot.link == lunaredu::lunaros::NetworkLinkState::Down);
  CHECK(snapshot.provisioning ==
        lunaredu::lunaros::NetworkProvisioningState::Idle);
  CHECK(snapshot.local_ip.empty());
  CHECK(service.stop(error));
}

} // namespace

int main() {
  test_network_service_owns_orthogonal_state();
  if (g_failures != 0) {
    std::cerr << g_failures << " network service checks failed" << std::endl;
    return 1;
  }
  std::cout << "1 LunarOS network service check passed" << std::endl;
  return 0;
}
