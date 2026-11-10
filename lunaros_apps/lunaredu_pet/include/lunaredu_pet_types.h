#pragma once

#include <cstdint>
#include "lunaredu_lunaros/model/expression.h"

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

} // namespace lunaredu::pet
