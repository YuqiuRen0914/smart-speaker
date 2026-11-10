#pragma once
#include "lunaredu_pet_types.h"
#include "lunaredu_pet_conversation.h"

namespace lunaredu::pet {
bool post_conversation_event(const conversation::AssistantEvent &event);
PetConversationPhase conversation_phase_from_snapshot(
    const conversation::ServiceRuntimeSnapshot &snapshot);
} // namespace lunaredu::pet
