#pragma once

#include <cstdint>

namespace lunaredu::pet {

enum class PetConversationPhase : uint8_t {
  Disabled,
  Activating,
  Connecting,
  Ready,
  Listening,
  Thinking,
  Speaking,
  Suspended,
  Failed,
};

const char *pet_conversation_phase_name(PetConversationPhase phase);

} // namespace lunaredu::pet
