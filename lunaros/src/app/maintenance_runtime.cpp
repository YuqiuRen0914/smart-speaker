#include "lunaredu_lunaros/app/maintenance.h"
#include <algorithm>
#include <cstdio>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "lunaredu_lunaros/device_api/device_api.h"
#include "app/maintenance_io.h"
#include "lunaredu_lunaros/delivery/resources.h"
#include "lunaredu_lunaros/kernel/runtime.h"
#include "lunaredu_lunaros/kernel/service_ids.h"
#include "lunaredu_lunaros/device_api/standard_projections.h"
#include "lunaredu_lunaros/persistence/storage.h"
#include "lunaredu_lunaros/persistence/settings.h"
#include "lunaredu_lunaros/delivery/update.h"
#include "lunaredu_lunaros/persistence/system_runtime.h"

namespace lunaredu::lunaros {
namespace {
uint32_t now_ms() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }
}
bool prepare_maintenance_runtime(NetworkPolicy policy, std::string &error) {
  policy.station_enabled = true;
  policy.provisioning.enabled = true;
  policy.provisioning.start_when_credentials_missing = true;
  policy.provisioning.start_when_connection_times_out = true;
  policy.provisioning.reconnect_failure_timeout_ms = 15000;
  policy.provisioning.reconnect_failure_min_attempts = 3;
  policy.synchronize_clock_on_connect = true;
  policy.require_tls_for_production = true;
  return setup_core_runtime({{kMainServiceExecutor, "product.main", 8},
      {kNetworkServiceExecutor, "network.runtime", 8},
      {kNetworkIoServiceExecutor, "network.runtime", 2}}, error) &&
      bind_service_executor_to_current_task(kMainServiceExecutor, &error) &&
      bind_service_executor_to_current_task(kNetworkServiceExecutor, &error) &&
      configure_network_runtime(policy, "CST-8", error);
}
std::array<ServiceNode, 7> maintenance_service_nodes() {
  return {system_service_node(), settings_service_node(), storage_service_node(),
      update_service_node(), network_service_node(), resource_service_node(), device_api_service_node()};
}
bool declare_maintenance_projections(void *, std::string &error) {
  return setup_standard_device_projections<standard_projection::services,
      standard_projection::tasks, standard_projection::identity, standard_projection::system,
      standard_projection::storage, standard_projection::network, standard_projection::update>(error);
}
void poll_maintenance_runtime(bool application_ready) {
  pump_service_executor(kMainServiceExecutor, 0);
  static uint32_t last_network_poll = 0;
  const auto now = now_ms();
  if (last_network_poll && now - last_network_poll < kNetworkPollIntervalMs) return;
  last_network_poll = now;
  pump_service_executor(kNetworkServiceExecutor, 0);
  poll_network_on_owner();
  static bool provisioning_reported = false;
  const auto network = network_snapshot();
  const bool provisioning = network.provisioning == NetworkProvisioningState::Active;
  if (provisioning && !provisioning_reported) {
    // Local console only; never publish credentials in the remotely readable log ring.
    printf("LunarOS setup Wi-Fi: %s password: %s URL: http://%s/setup\n",
        network.provisioning_ssid.c_str(), network_local_provisioning_password().c_str(),
        network.provisioning_ip.c_str());
  }
  provisioning_reported = provisioning;
  tick_maintenance_boot_confirmation(application_ready &&
                                     maintenance_io_healthy());
}
}
