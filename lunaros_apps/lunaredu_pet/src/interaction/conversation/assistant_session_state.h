#pragma once

#include <cstdint>

namespace lunaredu::pet::conversation::internal {

enum class AssistantSessionState : uint8_t {
  Inactive = 0,
  Opening,
  ReadyIdle,
  Listening,
  Speaking,
  Closing,
  Failed,
};

const char *assistant_session_state_name(AssistantSessionState state);
bool assistant_session_can_transition(AssistantSessionState from,
                                      AssistantSessionState to);
bool assistant_session_transition(AssistantSessionState &current,
                                  AssistantSessionState next);

} // namespace lunaredu::pet::conversation::internal

