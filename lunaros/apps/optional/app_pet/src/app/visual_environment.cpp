#include "diagnostics/function_probe.h"
#include "lunaredu_pet_visual_environment.h"
#include "lunaros_platform_link.h"
#include "lunaros_network.h"
#if LUNAROS_PET_PROVISIONING_COMPONENT
#include "lunaros_provisioning.h"
#endif
#include <esp_timer.h>

namespace lunaredu::pet {
VisualEnvironment read_platform_visual_environment(void *) {
  lunaredu::system::probe::Scope diagnostic_scope(lunaredu::system::probe::Point::VisualEnvironment);
  using namespace lunaros;
  VisualEnvironment environment;
  const auto network = network_fast_snapshot();
  const auto binding = platform_link_binding_status();
  const bool authority_ready = platform_link_local_authority_ready();
  environment.binding_committed = authority_ready &&
      binding.state == PlatformLinkBindingState::Bound &&
      !binding.persistence_pending && !binding.revocation_requested;
  #if LUNAROS_PET_PROVISIONING_COMPONENT
  environment.provisioning = provisioning_fast_snapshot().state==ProvisioningState::Active;
  #endif
  if (network.link != NetworkLinkState::Up || !network.station_has_address)
    environment.link = VisualLinkState::Offline;
  // A Wi-Fi address is not proof of platform connectivity or accepted binding.
  // PairingDevice must keep showing connection work after TLS/auth failures.
  else if (!authority_ready || binding.persistence_pending ||
           binding.revocation_requested ||
           binding.state == PlatformLinkBindingState::Connecting ||
           binding.state == PlatformLinkBindingState::Error ||
           platform_link_platform_connection_state() != PlatformLinkPlatformConnectionState::Connected)
    environment.link = VisualLinkState::Offline;
  else if (binding.state == PlatformLinkBindingState::Bound&&!binding.persistence_pending&&!binding.revocation_requested)
    environment.link = VisualLinkState::Bound;
  else if (binding.state == PlatformLinkBindingState::Pairing) {
    environment.link = platform_link_pairing_presentation().code.empty()?VisualLinkState::OnlineUnbound:VisualLinkState::AwaitingPairing;
    environment.pairing_remaining_fraction = platform_link_pairing_remaining_fraction(
        static_cast<uint32_t>(esp_timer_get_time() / 1000ULL));
  } else environment.link = VisualLinkState::OnlineUnbound;
  return environment;
}
} // namespace lunaredu::pet
