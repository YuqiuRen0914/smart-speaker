#include "behavior/emotion.h"

namespace lunaredu::pet {
namespace {

bool contains(std::string_view text, std::string_view needle) {
  return text.find(needle) != std::string_view::npos;
}

const char *semantic_state(std::string_view emotion) {
  if (emotion.empty()) return nullptr;
  // These session identifiers were accepted as interactions before the
  // coarse Expression adapter was removed. They remain non-visual reactions.
  if (emotion == "idle" || emotion == "activating" ||
      emotion == "connecting") return "idle";
  if (emotion == "session_thinking") return "thinking";
  if (contains(emotion, "neutral")) return "neutral";
  if (contains(emotion, "celebrate") || contains(emotion, "victory") ||
      contains(emotion, "congrat") || emotion == "win") return "celebrate";
  if (contains(emotion, "excited")) return "excited";
  if (contains(emotion, "laugh")) return "laughing";
  if (contains(emotion, "funny")) return "funny";
  if (contains(emotion, "loving")) return "loving";
  if (contains(emotion, "embarrass")) return "embarrassed";
  if (contains(emotion, "confident")) return "confident";
  if (contains(emotion, "delicious")) return "delicious";
  if (contains(emotion, "cry")) return "crying";
  if (contains(emotion, "silly")) return "silly";
  if (contains(emotion, "shock")) return "shocked";
  if (contains(emotion, "surprise") || contains(emotion, "exclaim"))
    return "surprised";
  if (contains(emotion, "winking")) return "winking";
  if (contains(emotion, "relaxed")) return "relaxed";
  if (contains(emotion, "confus") || contains(emotion, "question"))
    return "confused";
  if (contains(emotion, "sleep") || contains(emotion, "tired"))
    return "sleepy";
  if (contains(emotion, "sad") || contains(emotion, "sigh")) return "sad";
  if (contains(emotion, "angry")) return "angry";
  if (contains(emotion, "think") || contains(emotion, "curious"))
    return "thinking";
  if (contains(emotion, "happy") || contains(emotion, "smile"))
    return "happy";
  if (contains(emotion, "error") || contains(emotion, "alert") ||
      contains(emotion, "scared")) return "alert";
  if (contains(emotion, "listen")) return "listening";
  if (contains(emotion, "speak") || contains(emotion, "talk")) return "idle";
  return nullptr;
}

} // namespace

bool try_pet_theme_state_from_emotion(std::string_view emotion,
                                      const char *&state) {
  state = semantic_state(emotion);
  if (state != nullptr) return true;
  state = "idle";
  return false;
}

const char *pet_theme_state_for_conversation_phase(PetConversationPhase phase) {
  switch (phase) {
  case PetConversationPhase::Activating: return "activating";
  case PetConversationPhase::Connecting: return "connecting";
  case PetConversationPhase::Listening: return "listening";
  case PetConversationPhase::Thinking: return "session_thinking";
  case PetConversationPhase::Speaking: return "idle";
  case PetConversationPhase::Failed: return "alert";
  case PetConversationPhase::Disabled:
  case PetConversationPhase::Ready:
  case PetConversationPhase::Suspended: return "idle";
  }
  return "idle";
}

const char *pet_conversation_phase_name(PetConversationPhase phase) {
  switch (phase) {
  case PetConversationPhase::Disabled: return "disabled";
  case PetConversationPhase::Activating: return "activating";
  case PetConversationPhase::Connecting: return "connecting";
  case PetConversationPhase::Ready: return "ready";
  case PetConversationPhase::Listening: return "listening";
  case PetConversationPhase::Thinking: return "thinking";
  case PetConversationPhase::Speaking: return "speaking";
  case PetConversationPhase::Suspended: return "suspended";
  case PetConversationPhase::Failed: return "failed";
  }
  return "disabled";
}

} // namespace lunaredu::pet
