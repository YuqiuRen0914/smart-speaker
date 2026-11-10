#include "behavior_state.h"
#include "behavior/emotion.h"
namespace lunaredu::pet::behavior {
namespace {
bool pending(uint32_t now, uint32_t until) { return static_cast<int32_t>(now - until) < 0; }
constexpr uint32_t kSameEmotionCooldownMs = 12000;

bool elapsed(uint32_t now, uint32_t then, uint32_t duration) {
  return static_cast<int32_t>(now - then) >= static_cast<int32_t>(duration);
}

bool is_non_visual_theme(const std::string &theme) {
  return theme == "idle" || theme == "neutral" || theme == "relaxed";
}

const char *theme_for_mood(PetMood mood) {
  switch (mood) {
  case PetMood::Neutral: return "neutral";
  case PetMood::Happy: return "happy";
  case PetMood::Curious: return "thinking";
  case PetMood::Sleepy: return "sleepy";
  case PetMood::Sad: return "sad";
  case PetMood::Angry: return "angry";
  }
  return "neutral";
}

}
const char *task_state_for_activity_phase(const std::string &phase) {
  // Only named, observable activity phases may select a task animation.
  // Never turn an arbitrary transport message into visual progress.
  if (phase == "recognized" || phase == "understanding" || phase == "working")
    return "working";
  if (phase == "searching") return "searching";
  if (phase == "writing") return "writing";
  if (phase == "sending") return "sending";
  if (phase == "receiving") return "receiving";
  if (phase == "uploading") return "uploading";
  return nullptr;
}
Reaction react(const std::string &emotion) {
  Reaction result;
  const char *theme = nullptr;
  if (!try_pet_theme_state_from_emotion(emotion, theme)) return result;
  result.accepted = true;
  result.theme = theme;
  return result;
}
Reaction observe_emotion(State &state, const std::string &emotion, uint32_t now) {
  auto result = react(emotion);
  if (result.accepted) {
    result.present = !is_non_visual_theme(result.theme);
    if (result.present && state.has_presented_emotion &&
        state.last_presented_theme == result.theme &&
        !elapsed(now, state.last_presented_emotion_ms, kSameEmotionCooldownMs)) {
      result.present = false;
    }
    if (result.present) {
      state.emotion_active = true;
      state.emotion_theme = result.theme;
      state.emotion_until_ms = now + 2600;
      state.has_presented_emotion = true;
      state.last_presented_theme = result.theme;
      state.last_presented_emotion_ms = now;
    }
  }
  return result;
}

void reset_interactions(State &state) {
  state.emotion_active = false;
  state.emotion_theme = "idle";
  state.emotion_until_ms = 0;
  state.has_presented_emotion = false;
  state.last_presented_theme.clear();
  state.last_presented_emotion_ms = 0;
}
Decision evaluate(State &state, const Input &input) {
  Decision result;
  state.boot_complete = state.boot_complete || input.boot_progress >= 100;
  result.boot_complete = state.boot_complete;
  result.boot_progress = result.boot_complete ? 100 : input.boot_progress;
  if (!result.boot_complete) return result;
  if (input.transient_active) {
    if (input.transient_theme) result.theme = *input.transient_theme;
    result.transient = true; return result;
  }
  if (state.emotion_active) {
    const bool response_owns_emotion = input.conversation_phase == PetConversationPhase::Speaking ||
        input.conversation_phase == PetConversationPhase::Thinking;
    if (response_owns_emotion || pending(input.now_ms, state.emotion_until_ms)) {
      result.theme = state.emotion_theme;
      result.transient = true; return result;
    }
    state.emotion_active = false;
  }
  if (input.conversation_phase == PetConversationPhase::Thinking &&
      input.activity_state != nullptr && !input.activity_state->empty()) {
    result.theme = *input.activity_state;
    return result;
  }
  if(input.background_working&&(input.conversation_phase==PetConversationPhase::Ready||input.conversation_phase==PetConversationPhase::Disabled)){
    result.theme="working";return result;
  }
  result.theme = pet_theme_state_for_conversation_phase(input.conversation_phase);
  if (input.conversation_phase == PetConversationPhase::Ready ||
      input.conversation_phase == PetConversationPhase::Disabled ||
      input.conversation_phase == PetConversationPhase::Suspended) {
    if (input.mood != PetMood::Neutral)
      result.theme = theme_for_mood(input.mood);
  }
  // Speaking without a response emotion has a neutral face.
  return result;
}
} // namespace lunaredu::pet::behavior
