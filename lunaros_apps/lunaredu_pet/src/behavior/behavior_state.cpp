#include "behavior_state.h"
#include "lunaredu_pet_emotion.h"
#include <string_view>
namespace lunaredu::pet::behavior {
namespace {
bool pending(uint32_t now, uint32_t until) { return static_cast<int32_t>(now - until) < 0; }
}
bool mood_from_emotion(const std::string &emotion, PetMood &mood) {
  const std::string_view theme = pet_theme_state_from_emotion(emotion);
  if (theme == "happy" || theme == "laughing" || theme == "loving" ||
      theme == "funny" || theme == "confident" || theme == "delicious") mood = PetMood::Happy;
  else if (theme == "neutral" || theme == "relaxed") mood = PetMood::Neutral;
  else if (theme == "sad" || theme == "crying") mood = PetMood::Sad;
  else if (theme == "angry") mood = PetMood::Angry;
  else if (theme == "sleepy") mood = PetMood::Sleepy;
  else if (theme == "thinking" || theme == "surprised" || theme == "confused") mood = PetMood::Curious;
  else return false; // Connection, error and presentation states are not moods.
  return true;
}
Reaction react(const std::string &emotion) {
  Reaction result;
  if (!try_pet_expression_from_emotion(emotion, result.expression)) return result;
  result.accepted = true;
  result.theme = pet_theme_state_from_emotion(emotion);
  return result;
}
Reaction observe_emotion(State &state, const std::string &emotion, uint32_t now) {
  auto result = react(emotion);
  if (result.accepted) {
    state.emotion_active = true;
    state.emotion_expression = result.expression;
    state.emotion_theme = result.theme;
    state.emotion_until_ms = now + 2600;
  }
  return result;
}
void reset_interactions(State &state) {
  state.emotion_active = false;
  state.emotion_expression = lunaredu::presentation::Expression::Idle;
  state.emotion_theme = "idle";
  state.emotion_until_ms = 0;
}
Decision evaluate(State &state, const Input &input) {
  Decision result;
  state.boot_complete = state.boot_complete || input.boot_progress >= 100;
  result.boot_complete = state.boot_complete;
  result.boot_progress = result.boot_complete ? 100 : input.boot_progress;
  if (!result.boot_complete) { result.expression = lunaredu::presentation::Expression::Boot; return result; }
  if (input.transient_active) {
    result.expression = input.transient_expression;
    if (input.transient_theme) result.theme = *input.transient_theme;
    result.transient = true; return result;
  }
  if (state.emotion_active) {
    const bool response_owns_emotion = input.conversation_phase == PetConversationPhase::Speaking ||
        input.conversation_phase == PetConversationPhase::Thinking;
    if (response_owns_emotion || pending(input.now_ms, state.emotion_until_ms)) {
      result.expression = state.emotion_expression; result.theme = state.emotion_theme;
      result.transient = true; return result;
    }
    state.emotion_active = false;
  }
  result.theme = pet_theme_state_for_conversation_phase(input.conversation_phase);
  if (input.conversation_phase == PetConversationPhase::Thinking) result.expression = lunaredu::presentation::Expression::Curious;
  else if (input.conversation_phase == PetConversationPhase::Listening || input.conversation_phase == PetConversationPhase::Connecting)
    result.expression = lunaredu::presentation::Expression::Listening;
  else if (input.conversation_phase == PetConversationPhase::Failed) result.expression = lunaredu::presentation::Expression::Error;
  if (input.conversation_phase == PetConversationPhase::Ready ||
      input.conversation_phase == PetConversationPhase::Disabled ||
      input.conversation_phase == PetConversationPhase::Suspended) {
    const auto reaction = react(pet_mood_name(input.mood));
    if (reaction.accepted && input.mood != PetMood::Neutral) {
      result.expression = reaction.expression;
      result.theme = reaction.theme;
    }
  }
  // Speaking without a response emotion has a neutral face.
  return result;
}
} // namespace lunaredu::pet::behavior
