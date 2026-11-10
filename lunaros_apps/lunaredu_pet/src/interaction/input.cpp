#include "lunaredu_pet_conversation_events.h"
#include "lunaredu_pet_interaction.h"

namespace lunaredu::pet {
bool post_conversation_event(const conversation::AssistantEvent &event) {
  if (event.kind == conversation::AssistantEventKind::Transcript) return true;
  const char *detail = event.detail;
  if (event.error && event.error[0]) detail = event.error;
  else if (event.result && event.result[0]) detail = event.result;
  return post_interaction_event({
      event.kind == conversation::AssistantEventKind::Emotion
          ? InteractionEventKind::Emotion : InteractionEventKind::Status,
      event.value, detail});
}
} // namespace lunaredu::pet
