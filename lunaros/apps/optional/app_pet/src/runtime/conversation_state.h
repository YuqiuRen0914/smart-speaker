#pragma once
#include "lunaredu_pet_types.h"
namespace lunaredu::pet::runtime {
// Called by the internal conversation service after releasing its own mutex.
// Product environment projections do not own this state.
void update_conversation_phase(PetConversationPhase phase);
}
