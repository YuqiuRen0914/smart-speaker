#include "lunaredu_pet_conversation_events.h"
#include <iostream>

#include "lunaredu_pet_service.h"

namespace {

using lunaredu::pet::PetConversationPhase;
using lunaredu::pet::conversation_phase_from_snapshot;
using lunaredu::pet::conversation::ServiceRuntimeSnapshot;

bool expect(const ServiceRuntimeSnapshot &snapshot,
            PetConversationPhase expected) {
  return conversation_phase_from_snapshot(snapshot) == expected;
}

} // namespace

int main() {
  ServiceRuntimeSnapshot snapshot;
  if (!expect(snapshot, PetConversationPhase::Disabled)) return 1;

  snapshot.initialized = true;
  if (!expect(snapshot, PetConversationPhase::Disabled)) return 2;

  snapshot.enabled = true;
  if (!expect(snapshot, PetConversationPhase::Connecting)) return 3;

  snapshot.activation_ready = true;
  snapshot.transport_connected = true;
  if (!expect(snapshot, PetConversationPhase::Ready)) return 13;
  snapshot.activation_ready = false;
  snapshot.transport_connected = false;

  snapshot.activation_pending = true;
  if (!expect(snapshot, PetConversationPhase::Activating)) return 4;

  snapshot.opening = true;
  if (!expect(snapshot, PetConversationPhase::Connecting)) return 5;

  snapshot.opening = false;
  snapshot.activation_pending = false;
  snapshot.ready = true;
  if (!expect(snapshot, PetConversationPhase::Ready)) return 6;

  snapshot.listening = true;
  if (!expect(snapshot, PetConversationPhase::Listening)) return 7;

  snapshot.thinking = true;
  if (!expect(snapshot, PetConversationPhase::Thinking)) return 8;

  snapshot.speaking = true;
  if (!expect(snapshot, PetConversationPhase::Speaking)) return 9;

  snapshot.failed = true;
  if (!expect(snapshot, PetConversationPhase::Failed)) return 10;

  snapshot.suspended = true;
  if (!expect(snapshot, PetConversationPhase::Suspended)) return 11;

  snapshot.enabled = false;
  if (!expect(snapshot, PetConversationPhase::Disabled)) return 12;

  std::cout << "12 Pet conversation phase checks passed" << std::endl;
  return 0;
}
