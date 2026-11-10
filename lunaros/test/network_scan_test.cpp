// Real scan coordinator, scheduler and HTTP binding; only hardware/time are stubbed.
#include <cassert>
#include <cstring>
#include <iostream>
#include "network/network_job_manager_internal.h"
#include "network/network_scan.cpp"
#include "device_api/projections/provisioning_projection.cpp"
#include "device_api/projections/network_projection.cpp"

using namespace lunaredu::lunaros;
namespace {
uint32_t now_ms = 100;
bool owner = false, started = true, station = true, dispatch = true;
bool driver_ok = true, cancel_in_driver = false, reset_in_driver = false;
unsigned driver_calls = 0, busy_attempts = 0;
int record_count = 30;
void reset_case() {
  reset_network_job_manager_internal();
  reset_network_wifi_scan_internal();
  now_ms = 100; owner = false; started = station = dispatch = driver_ok = true;
  cancel_in_driver = reset_in_driver = false;
  driver_calls = busy_attempts = 0; record_count = 30;
}
}
namespace lunaredu::lunaros {
NetworkPolicy network_policy() { NetworkPolicy p; p.station_enabled = station; return p; }
NetworkSnapshot network_snapshot() { NetworkSnapshot s; s.service_started = started; return s; }
bool current_task_owns_service_executor() { return owner; }
DeviceConfig device_config() { return {}; }
ProvisioningWifiSnapshot provisioning_driver_wifi_snapshot() { return {}; }
int32_t provisioning_driver_wifi_rssi() { return 0; }
int provisioning_driver_wifi_channel() { return 0; }
bool network_provisioning_softap_active() { return true; }
const char *network_provisioning_mode_name(NetworkProvisioningMode) { return "softap"; }
uint32_t schedule_network_provisioning_restart() { return 0; }
NetworkProvisioningCredentialResult submit_network_provisioning_wifi_credentials(const char *, const char *) {
  return NetworkProvisioningCredentialResult::Ok;
}
void fill_saved_wifi_config_payload(JsonObject, const DeviceWifiConfig &, bool) {}
void fill_network_service_status_payload(JsonObject) {}
void fill_network_service_bootstrap_payload(JsonObject) {}
void fill_network_config_payload(JsonObject) {}
DeviceOperationResult handle_network_config_set(JsonVariantConst, JsonObject) { return {}; }
DeviceOperationResult handle_network_config_clear(JsonVariantConst, JsonObject) { return {}; }
const DeviceCapabilityDescriptor &network_service_capability_descriptor() {
  static DeviceCapabilityDescriptor descriptor; return descriptor;
}
bool register_device_projection(const DeviceProjectionContribution &, std::string *) { return true; }
bool deactivate_device_capability(const char *) { return true; }
namespace internal {
uint32_t system_monotonic_ms() { return now_ms; }
void system_delay_ms(uint32_t delay) {
  now_ms += delay;
  // The HTTP caller yields; only the existing background worker scans.
  if (dispatch) (void)run_next_network_job(now_ms);
}
}
}
bool wifi_scan_access_points(std::vector<WifiScanApRecord> &records, std::string &error) {
  assert(network_job_runner_context_active());
  ++driver_calls;
  NetworkWifiScanSnapshot nested;
  std::string nested_error;
  assert(!request_network_wifi_scan_and_wait(nested, nested_error));
  assert(nested_error == "wifi_scan_wait_requires_external_caller");
  if (cancel_in_driver) { assert(cancel_network_job(g_scan_job, error)); }
  if (reset_in_driver) reset_network_wifi_scan_internal();
  if (busy_attempts > 0) { --busy_attempts; error = "wifi_radio_busy"; return false; }
  if (!driver_ok) { error = "wifi_scan_failed"; return false; }
  for (int i = 0; i < record_count; ++i) records.push_back({"ssid-" + std::to_string(i), -40-i, 3, true, 6});
  return true;
}
int main() {
  std::string error;
  NetworkWifiScanSnapshot result;
  reset_case();
  assert(request_network_wifi_scan(error));
  assert(request_network_wifi_scan(error));
  assert(driver_calls == 0 && network_job_manager_snapshot().active_count == 1);
  assert(request_network_wifi_scan_and_wait(result, error));
  assert(driver_calls == 1 && result.access_point_count == 24);
  assert(result.access_points[0].channel == 6 && result.access_points[0].authmode == 3);
  assert(network_job_manager_snapshot().record_count == 0);

  reset_case();
  JsonDocument request, response;
  auto status = handle_network_provision_scan(request.as<JsonVariantConst>(), response.to<JsonObject>());
  assert(status.ok && response["count"] == 24);
  assert(response["aps"][0]["ssid"] == "ssid-0");
  assert(response["aps"][0]["rssi"] == -40 && response["aps"][0]["channel"] == 6);
  assert(response["aps"][0]["authmode"] == 3 && response["aps"][0]["secure"] == true);
  bool found = false;
  for (const auto &operation : kNetworkOperations) {
    if (std::strcmp(operation.model.id, "provision.scan") == 0) {
      found = true;
      assert(operation.model.execution == DeviceOperationExecution::Caller);
      assert(operation.projection.method == DeviceOperationMethod::Post);
    }
  }
  assert(found);
  record_count = 0;
  response.clear();
  status = handle_network_provision_scan(request.as<JsonVariantConst>(), response.to<JsonObject>());
  assert(status.ok && response["count"] == 0 && response["aps"].size() == 0);
  dispatch = false;
  response.clear();
  status = handle_network_provision_scan(request.as<JsonVariantConst>(), response.to<JsonObject>());
  assert(!status.ok && status.status_code == 408);

  reset_case(); owner = true;
  assert(!request_network_wifi_scan_and_wait(result, error));
  assert(error == "wifi_scan_wait_requires_external_caller" && driver_calls == 0);
  owner = false; started = false;
  assert(!request_network_wifi_scan(error));
  started = true; station = false;
  assert(!request_network_wifi_scan(error));
  reset_case();
  assert(!request_network_wifi_scan_and_wait(result, error, 0));
  assert(!request_network_wifi_scan_and_wait(result, error, 15001));
  dispatch = false;
  assert(!request_network_wifi_scan_and_wait(result, error, 20));
  assert(error == "wifi_scan_timeout" && network_job_active(g_scan_job));
  assert(run_next_network_job(now_ms)); // no dangling waiter context after timeout
  assert(network_wifi_scan_snapshot().state == NetworkWifiScanState::Ready);

  reset_case(); busy_attempts = 2;
  assert(request_network_wifi_scan_and_wait(result, error));
  assert(driver_calls == 3 && network_job_manager_snapshot().metrics.retried == 2);
  reset_case(); busy_attempts = 100;
  assert(!request_network_wifi_scan_and_wait(result, error));
  assert(driver_calls <= 60 && !network_job_active(g_scan_job));

  reset_case(); driver_ok = false;
  assert(!request_network_wifi_scan_and_wait(result, error));
  assert(error == "wifi_scan_failed" && result.access_point_count == 0);
  driver_ok = true;
  assert(request_network_wifi_scan_and_wait(result, error));
  assert(begin_network_ota_maintenance(error));
  assert(!request_network_wifi_scan(error));
  assert(network_wifi_scan_snapshot().access_point_count == 0);

  reset_case(); assert(request_network_wifi_scan(error));
  assert(cancel_network_job(g_scan_job, error));
  assert(network_wifi_scan_snapshot().state == NetworkWifiScanState::Failed);
  assert(request_network_wifi_scan_and_wait(result, error));
  reset_case(); assert(request_network_wifi_scan(error));
  now_ms += 15001; (void)run_next_network_job(now_ms);
  assert(network_wifi_scan_snapshot().state == NetworkWifiScanState::Failed);
  assert(driver_calls == 0);
  reset_case(); cancel_in_driver = true;
  assert(!request_network_wifi_scan_and_wait(result, error));
  assert(error == "wifi_scan_cancelled");
  reset_case(); reset_in_driver = true;
  assert(!request_network_wifi_scan_and_wait(result, error));
  assert(error == "wifi_scan_superseded");
  assert(network_wifi_scan_snapshot().state == NetworkWifiScanState::Idle);

  reset_case();
  set_network_job_wake_handler([](void *) { (void)run_next_network_job(now_ms); }, nullptr);
  assert(request_network_wifi_scan_and_wait(result, error));
  assert(driver_calls == 1); // completion may race submission's return
  std::cout << "Shared scan scheduling, HTTP wire fields, owner guards, timeout, cancellation and generation tests passed\n";
}
