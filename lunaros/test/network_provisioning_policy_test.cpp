#include <cassert>
#include <cstdint>
#include <iostream>
#include <limits>

#include "network/provisioning/provisioning_credentials.h"

using namespace lunaredu::lunaros;
#include "network/provisioning/provisioning_restart.h"

namespace {

void test_submitted_credential_is_first_and_unique() {
  const DeviceWifiCredential saved[] = {
      {"Office", "old-password"},
      {"Guest", "guest-password"},
      {"Office", "duplicate-password"},
  };
  const DeviceWifiCredential submitted{"Office", "new-password"};
  DeviceWifiCredential output[5];

  const size_t count = build_provisioning_wifi_credentials(
      submitted, saved, 3, output, 5);

  assert(count == 2);
  assert(output[0].ssid == "Office");
  assert(output[0].password == "new-password");
  assert(output[1].ssid == "Guest");
}

void test_blank_credentials_are_ignored_and_capacity_is_respected() {
  const DeviceWifiCredential saved[] = {
      {"", "ignored"}, {"Home", "home"}, {"Lab", "lab"}};
  const DeviceWifiCredential submitted{"School", "school"};
  DeviceWifiCredential output[2];

  const size_t count = build_provisioning_wifi_credentials(
      submitted, saved, 3, output, 2);

  assert(count == 2);
  assert(output[0].ssid == "School");
  assert(output[1].ssid == "Home");
  assert(build_provisioning_wifi_credentials(submitted, saved, 3, nullptr, 2) ==
         0);
}

void test_blank_submission_is_rejected_by_policy_builder() {
  const DeviceWifiCredential submitted{"", "password"};
  DeviceWifiCredential output[2];
  assert(build_provisioning_wifi_credentials(submitted, nullptr, 0, output, 2) ==
         0);
}

void test_restart_schedule_clamps_and_consumes_once() {
  ProvisioningRestartSchedule schedule;
  const uint32_t effective_delay = schedule.schedule(1000, 10);
  assert(effective_delay == kProvisioningMinimumRestartDelayMs);
  assert(schedule.pending());
  assert(!schedule.consume_if_due(1199));
  assert(schedule.consume_if_due(1200));
  assert(!schedule.pending());
  assert(!schedule.consume_if_due(1201));
}

void test_restart_schedule_handles_clock_wraparound() {
  ProvisioningRestartSchedule schedule;
  const uint32_t start = std::numeric_limits<uint32_t>::max() - 99;
  assert(schedule.schedule(start, 200) == 200);
  assert(!schedule.consume_if_due(99));
  assert(schedule.consume_if_due(100));
}

} // namespace

int main() {
  test_submitted_credential_is_first_and_unique();
  test_blank_credentials_are_ignored_and_capacity_is_respected();
  test_blank_submission_is_rejected_by_policy_builder();
  const DeviceWifiCredential spaced[] = {{"Office", "one"}, {"Office ", "two"}};
  const DeviceWifiCredential input{" Office ", "three"};
  DeviceWifiCredential preserved[5];
  assert(build_provisioning_wifi_credentials(input, spaced, 2, preserved, 5) == 3);
  assert(preserved[0].ssid == " Office " && preserved[1].ssid == "Office" &&
         preserved[2].ssid == "Office ");
  test_restart_schedule_clamps_and_consumes_once();
  test_restart_schedule_handles_clock_wraparound();
  std::cout << "network provisioning policy tests passed: 5\n";
  return 0;
}
