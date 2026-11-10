#include "interaction/conversation/assistant_session_state.h"

namespace lunaredu::pet::conversation::internal {

const char *assistant_session_state_name(AssistantSessionState state) {
  switch (state) {
  case AssistantSessionState::Opening:
    return "opening";
  case AssistantSessionState::ReadyIdle:
    return "ready_idle";
  case AssistantSessionState::Listening:
    return "listening";
  case AssistantSessionState::Speaking:
    return "speaking";
  case AssistantSessionState::Closing:
    return "closing";
  case AssistantSessionState::Failed:
    return "failed";
  case AssistantSessionState::Inactive:
  default:
    return "inactive";
  }
}

bool assistant_session_can_transition(AssistantSessionState from,
                                      AssistantSessionState to) {
  if (from == to) {
    return true;
  }
  switch (from) {
  case AssistantSessionState::Inactive:
    return to == AssistantSessionState::Opening ||
           to == AssistantSessionState::ReadyIdle ||
           to == AssistantSessionState::Listening ||
           to == AssistantSessionState::Failed;
  case AssistantSessionState::Opening:
    return to == AssistantSessionState::ReadyIdle ||
           to == AssistantSessionState::Listening ||
           to == AssistantSessionState::Speaking ||
           to == AssistantSessionState::Failed ||
           to == AssistantSessionState::Inactive;
  case AssistantSessionState::ReadyIdle:
    return to == AssistantSessionState::Listening ||
           to == AssistantSessionState::Speaking ||
           to == AssistantSessionState::Closing ||
           to == AssistantSessionState::Failed ||
           to == AssistantSessionState::Inactive;
  case AssistantSessionState::Listening:
    return to == AssistantSessionState::Speaking ||
           to == AssistantSessionState::ReadyIdle ||
           to == AssistantSessionState::Closing ||
           to == AssistantSessionState::Failed ||
           to == AssistantSessionState::Inactive;
  case AssistantSessionState::Speaking:
    return to == AssistantSessionState::ReadyIdle ||
           to == AssistantSessionState::Listening ||
           to == AssistantSessionState::Closing ||
           to == AssistantSessionState::Failed ||
           to == AssistantSessionState::Inactive;
  case AssistantSessionState::Closing:
    return to == AssistantSessionState::Inactive ||
           to == AssistantSessionState::ReadyIdle ||
           to == AssistantSessionState::Failed ||
           to == AssistantSessionState::Opening;
  case AssistantSessionState::Failed:
    return to == AssistantSessionState::Inactive ||
           to == AssistantSessionState::Opening;
  default:
    return false;
  }
}

bool assistant_session_transition(AssistantSessionState &current,
                                  AssistantSessionState next) {
  if (!assistant_session_can_transition(current, next)) {
    return false;
  }
  current = next;
  return true;
}

} // namespace lunaredu::pet::conversation::internal

