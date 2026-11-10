#pragma once

#include "interaction/conversation/assistant_events.h"

namespace lunaredu::pet::conversation::internal {

void setup_assistant_loop();
bool post_assistant_event_locked(AssistantEvent event);
bool post_assistant_event(AssistantEvent event);
void apply_microphone_policy_change(bool enabled);
void tick_assistant_loop();

} // namespace lunaredu::pet::conversation::internal
