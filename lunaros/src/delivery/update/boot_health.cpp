#include "lunaredu_lunaros/delivery/boot_health.h"
#include "lunaredu_lunaros/delivery/update.h"
#include "lunaredu_lunaros/kernel/service_ids.h"
#include "lunaredu_lunaros/kernel/service_registry.h"
#include "lunaredu_lunaros/network/network.h"
#include <esp_timer.h>
namespace lunaredu::lunaros {
void poll_boot_health(BootHealthGate &gate, bool application_ready) {
  tick_ota_service();
  const auto now = static_cast<uint32_t>(esp_timer_get_time() / 1000);
  if (!ota_service_boot_pending()) {
    (void)gate.ready(now, false, false);
    return;
  }
  const auto network = network_snapshot();
  const bool reachable =
      network.link == NetworkLinkState::Up ||
      (network.provisioning == NetworkProvisioningState::Active &&
       !network.provisioning_ip.empty());
  const bool healthy = application_ready && required_services_ready() &&
                       reachable && network.platform_ready &&
                       service_available(kDeviceApiServiceId);
  if (gate.ready(now, true, healthy))
    (void)ota_service_confirm_running_image();
}
} // namespace lunaredu::lunaros
