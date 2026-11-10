#include "interaction/conversation/product_system_state.h"

#include "interaction/transport/assistant_transport.h"
#include "interaction/conversation/assistant_core.h"

namespace lunaredu::pet::conversation::internal {

const char *product_system_state_name(ProductSystemState state) {
  switch (state) {
  case ProductSystemState::Provisioning:
    return "provisioning";
  case ProductSystemState::Offline:
    return "offline";
  case ProductSystemState::ActivationPending:
    return "activation_pending";
  case ProductSystemState::Connecting:
    return "connecting";
  case ProductSystemState::Ready:
    return "ready";
  case ProductSystemState::Recovering:
    return "recovering";
  case ProductSystemState::Booting:
  default:
    return "booting";
  }
}

ProductSystemState derive_product_system_state(const Runtime &runtime) {
  if (runtime.started_at_ms <= 0) {
    return ProductSystemState::Booting;
  }
  if (assistant_network_provisioning_active()) {
    return ProductSystemState::Provisioning;
  }
  if (!assistant_network_connected()) {
    return ProductSystemState::Offline;
  }
  if (runtime.activation.pending) {
    return ProductSystemState::ActivationPending;
  }
  if (!runtime.activation.ready || !runtime.transport.connected) {
    return ProductSystemState::Connecting;
  }
  if (!runtime.diagnostics.last_error.empty() &&
      runtime.session_state == AssistantSessionState::Failed) {
    return ProductSystemState::Recovering;
  }
  if (runtime.session_state == AssistantSessionState::Opening ||
      runtime.session_state == AssistantSessionState::Closing) {
    return ProductSystemState::Connecting;
  }
  if (runtime.session_state == AssistantSessionState::Failed) {
    return ProductSystemState::Recovering;
  }
  return ProductSystemState::Ready;
}

} // namespace lunaredu::pet::conversation::internal
