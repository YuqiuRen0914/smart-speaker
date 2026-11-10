#include <cassert>
#include <iostream>
#include <cstring>
#include "lunaredu_lunaros/app/maintenance.h"
#include "lunaredu_lunaros/delivery/update.h"
#include "lunaredu_lunaros/kernel/service_ids.h"

namespace {
int64_t clock_ms = 100;
bool required_ready = true, api_ready = true;
int confirmations = 0, ticks = 0, ota_starts = 0, network_reads = 0;
lunaredu::lunaros::NetworkSnapshot network;
lunaredu::lunaros::OtaServiceStatus ota;
}
int64_t esp_timer_get_time() { return clock_ms * 1000; }
namespace lunaredu::lunaros {
NetworkSnapshot network_snapshot() { ++network_reads; return network; }
bool required_services_ready(std::string *) { return required_ready; }
bool service_available(const char *) { return api_ready; }
bool ota_service_boot_pending() { return ota.pending_verify; }
void tick_ota_service() { ++ticks; }
void setup_ota_service() { ++ota_starts; }
OtaServiceStatus ota_service_status_snapshot() { return ota; }
bool ota_service_confirm_running_image() { ++confirmations; ota.pending_verify = false; return true; }
}
int main() {
  using namespace lunaredu::lunaros;
  const auto update = update_service_node();
  // Only system is healthy in this recovery scenario; resource storage is down.
  for (size_t i = 0; i < update.definition.dependency_count; ++i) {
    const auto &dependency = update.definition.dependencies[i];
    assert(!dependency.required || std::strcmp(dependency.id, kSystemRuntimeServiceId) == 0);
  }
  assert(update.start(update.context).disposition == ServiceStartDisposition::Ready && ota_starts == 1);
  ota.pending_verify = true;
  network.platform_ready = true;
  network.link = NetworkLinkState::Up;
  tick_maintenance_boot_confirmation(false);
  clock_ms += 5000; tick_maintenance_boot_confirmation(false);
  assert(confirmations == 0 && ticks == 2);
  tick_maintenance_boot_confirmation(true);
  clock_ms += 2999; tick_maintenance_boot_confirmation(true);
  assert(confirmations == 0);
  api_ready = false; tick_maintenance_boot_confirmation(true);
  api_ready = true; clock_ms += 1; tick_maintenance_boot_confirmation(true);
  clock_ms += 3000; tick_maintenance_boot_confirmation(true);
  assert(confirmations == 1);
  clock_ms += 3000; tick_maintenance_boot_confirmation(true);
  assert(confirmations == 1);
  ota.pending_verify = true;
  required_ready = false; tick_maintenance_boot_confirmation(true);
  required_ready = true;
  network.link = NetworkLinkState::Down;
  network.provisioning = NetworkProvisioningState::Active;
  clock_ms += 3000; tick_maintenance_boot_confirmation(true);
  assert(confirmations == 1); // No reachable provisioning address yet.
  network.provisioning_ip = "192.168.4.1";
  tick_maintenance_boot_confirmation(true);
  clock_ms += 3000; tick_maintenance_boot_confirmation(true);
  assert(confirmations == 2); // Provisioning is a valid offline recovery path.
  const int reads = network_reads;
  for (int i = 0; i < 100; ++i) tick_maintenance_boot_confirmation(true);
  assert(network_reads == reads); // Normal operation never copies network strings.
  std::cout << "Maintenance boot health, interrupted readiness, offline provisioning and idempotence passed\n";
}
