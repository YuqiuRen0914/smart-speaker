#include "lunaredu_pet_conversation_events.h"

namespace lunaredu::pet {

PetConversationPhase conversation_phase_from_snapshot(
    const lunaredu::pet::conversation::ServiceRuntimeSnapshot &assistant) {
  if (!assistant.initialized || !assistant.enabled) {
    return PetConversationPhase::Disabled;
  }
  if (assistant.suspended) {
    return PetConversationPhase::Suspended;
  }
  if (assistant.failed) {
    return PetConversationPhase::Failed;
  }
  if (assistant.speaking) {
    return PetConversationPhase::Speaking;
  }
  if (assistant.thinking) {
    return PetConversationPhase::Thinking;
  }
  if (assistant.listening) {
    return PetConversationPhase::Listening;
  }
  if (assistant.opening) {
    return PetConversationPhase::Connecting;
  }
  if (assistant.activation_pending) {
    return PetConversationPhase::Activating;
  }
  if (assistant.ready ||
      (assistant.activation_ready && assistant.transport_connected)) {
    return PetConversationPhase::Ready;
  }
  return PetConversationPhase::Connecting;
}

} // namespace lunaredu::pet
