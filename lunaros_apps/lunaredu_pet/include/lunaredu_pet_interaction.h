#pragma once

namespace lunaredu::pet {
enum class InteractionEventKind { Status, Emotion };
// Input strings are borrowed during the call and copied into a bounded queue.
struct InteractionEvent {
  InteractionEventKind kind = InteractionEventKind::Status;
  const char *value = nullptr;
  const char *detail = nullptr;
};
bool post_interaction_event(const InteractionEvent &event);
bool post_touch_reaction(const char *emotion);
} // namespace lunaredu::pet
