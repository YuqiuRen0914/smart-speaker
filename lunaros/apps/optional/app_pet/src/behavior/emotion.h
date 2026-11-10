#pragma once

#include "lunaredu_pet_types.h"

#include <string_view>

namespace lunaredu::pet {

// Normalize a supported semantic emotion to Pet's own state vocabulary.
// Unknown input is rejected and leaves state set to "idle".
bool try_pet_theme_state_from_emotion(std::string_view emotion,
                                      const char *&state);
const char *pet_theme_state_for_conversation_phase(PetConversationPhase phase);

} // namespace lunaredu::pet
