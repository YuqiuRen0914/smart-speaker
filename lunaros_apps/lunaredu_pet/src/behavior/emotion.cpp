#include "lunaredu_pet_emotion.h"
namespace lunaredu::pet {
namespace {bool contains(const std::string &text,const char *needle) {return text.find(needle)!=std::string::npos;}}
bool try_pet_expression_from_emotion(const std::string &emotion,
                                     lunaredu::presentation::Expression &expression) {
  if (emotion.empty()) {
    expression = lunaredu::presentation::Expression::Idle;
    return false;
  }
  lunaredu::presentation::Expression theme_expression = lunaredu::presentation::Expression::Idle;
  if (pet_expression_from_theme_state_id(emotion, theme_expression)) {
    expression = theme_expression;
    return true;
  }
  if (contains(emotion, "neutral") || contains(emotion, "relaxed") ||
      contains(emotion, "winking")) {
    expression = lunaredu::presentation::Expression::Idle;
    return true;
  }
  if (contains(emotion, "celebrate") || contains(emotion, "victory") ||
      contains(emotion, "congrat") || contains(emotion, "win")) {
    expression = lunaredu::presentation::Expression::Celebrate;
    return true;
  }
  if (contains(emotion, "happy") || contains(emotion, "laugh") ||
      contains(emotion, "funny") || contains(emotion, "loving") ||
      contains(emotion, "confident") || contains(emotion, "delicious") ||
      contains(emotion, "smile")) {
    expression = lunaredu::presentation::Expression::Happy;
    return true;
  }
  if (contains(emotion, "shock") || contains(emotion, "surprise") ||
      contains(emotion, "exclaim") || contains(emotion, "curious") ||
      contains(emotion, "think") || contains(emotion, "confus") ||
      contains(emotion, "question") || contains(emotion, "silly") ||
      contains(emotion, "embarrass")) {
    expression = lunaredu::presentation::Expression::Curious;
    return true;
  }
  if (contains(emotion, "sad") || contains(emotion, "sleep") ||
      contains(emotion, "tired") || contains(emotion, "cry") ||
      contains(emotion, "sigh")) {
    expression = lunaredu::presentation::Expression::Sleepy;
    return true;
  }
  if (contains(emotion, "angry") || contains(emotion, "error") ||
      contains(emotion, "alert") || contains(emotion, "scared")) {
    expression = lunaredu::presentation::Expression::Error;
    return true;
  }
  if (contains(emotion, "listen")) {
    expression = lunaredu::presentation::Expression::Listening;
    return true;
  }
  if (contains(emotion, "speak") || contains(emotion, "talk")) {
    expression = lunaredu::presentation::Expression::Speaking;
    return true;
  }
  expression = lunaredu::presentation::Expression::Idle;
  return false;
}

lunaredu::presentation::Expression pet_expression_from_emotion(const std::string &emotion) {
  lunaredu::presentation::Expression expression = lunaredu::presentation::Expression::Idle;
  (void)try_pet_expression_from_emotion(emotion, expression);
  return expression;
}

const char *pet_theme_state_from_emotion(const std::string &emotion) {
  if (emotion.empty()) return "idle";
  // Preserve every official XiaoZhi emotion as its own editable semantic
  // state. The lunaredu::presentation::Expression enum remains deliberately coarse because it is
  // shared across presentations; the semantic theme state is preserved.
  if (contains(emotion, "neutral")) return "neutral";
  if (contains(emotion, "laugh")) return "laughing";
  if (contains(emotion, "funny")) return "funny";
  if (contains(emotion, "loving")) return "loving";
  if (contains(emotion, "embarrass")) return "embarrassed";
  if (contains(emotion, "confident")) return "confident";
  if (contains(emotion, "delicious")) return "delicious";
  if (contains(emotion, "cry")) return "crying";
  if (contains(emotion, "silly")) return "silly";
  if (contains(emotion, "shock")) return "shocked";
  if (contains(emotion, "surprise") || contains(emotion, "exclaim")) {
    return "surprised";
  }
  if (contains(emotion, "winking")) return "winking";
  if (contains(emotion, "relaxed")) return "relaxed";
  if (contains(emotion, "confus") || contains(emotion, "question")) {
    return "confused";
  }
  if (contains(emotion, "sleep") || contains(emotion, "tired")) {
    return "sleepy";
  }
  if (contains(emotion, "sad") || contains(emotion, "sigh")) return "sad";
  if (contains(emotion, "angry")) return "angry";
  if (contains(emotion, "think") || contains(emotion, "curious")) {
    return "thinking";
  }
  if (contains(emotion, "happy") || contains(emotion, "smile")) {
    return "happy";
  }
  if (contains(emotion, "error") || contains(emotion, "alert") ||
      contains(emotion, "scared")) return "alert";
  if (contains(emotion, "listen")) return "listening";
  if (contains(emotion, "speak") || contains(emotion, "talk")) {
    return "idle";
  }
  return "idle";
}

const char *pet_theme_state_for_conversation_phase(PetConversationPhase phase) {
  switch (phase) {
  case PetConversationPhase::Activating:
    return "activating";
  case PetConversationPhase::Connecting:
    return "connecting";
  case PetConversationPhase::Listening:
    return "listening";
  case PetConversationPhase::Thinking:
    return "session_thinking";
  case PetConversationPhase::Speaking:
    return "idle";
  case PetConversationPhase::Failed:
    return "alert";
  case PetConversationPhase::Disabled:
  case PetConversationPhase::Ready:
  case PetConversationPhase::Suspended:
    return "idle";
  }
  return "idle";
}

const char *pet_theme_state_for_expression(lunaredu::presentation::Expression expression) {
  switch (expression) {
  case lunaredu::presentation::Expression::Listening:
    return "listening";
  case lunaredu::presentation::Expression::Speaking:
    return "idle";
  case lunaredu::presentation::Expression::Happy:
  case lunaredu::presentation::Expression::Celebrate:
    return "happy";
  case lunaredu::presentation::Expression::Curious:
    return "session_thinking";
  case lunaredu::presentation::Expression::Sleepy:
    return "sleepy";
  case lunaredu::presentation::Expression::Error:
    return "alert";
  case lunaredu::presentation::Expression::Boot:
  case lunaredu::presentation::Expression::Idle:
  case lunaredu::presentation::Expression::Moving:
    return "idle";
  }
  return "idle";
}

const char *pet_theme_event_for_state(const std::string &state_id) {
  if (state_id == "activating") return "pet.session.activating";
  if (state_id == "connecting") return "pet.session.connecting";
  if (state_id == "listening") return "pet.session.listening";
  if (state_id == "session_thinking") return "pet.session.thinking";
  if (state_id == "alert") return "system.alert";
  struct EmotionEvent {
    const char *state;
    const char *event;
  };
  static constexpr EmotionEvent kEmotionEvents[] = {
      {"neutral", "pet.emotion.neutral"},
      {"happy", "pet.emotion.happy"},
      {"laughing", "pet.emotion.laughing"},
      {"funny", "pet.emotion.funny"},
      {"loving", "pet.emotion.loving"},
      {"embarrassed", "pet.emotion.embarrassed"},
      {"confident", "pet.emotion.confident"},
      {"delicious", "pet.emotion.delicious"},
      {"sad", "pet.emotion.sad"},
      {"crying", "pet.emotion.crying"},
      {"sleepy", "pet.emotion.sleepy"},
      {"silly", "pet.emotion.silly"},
      {"angry", "pet.emotion.angry"},
      {"surprised", "pet.emotion.surprised"},
      {"shocked", "pet.emotion.shocked"},
      {"thinking", "pet.emotion.thinking"},
      {"winking", "pet.emotion.winking"},
      {"relaxed", "pet.emotion.relaxed"},
      {"confused", "pet.emotion.confused"},
  };
  for (const auto &emotion : kEmotionEvents) {
    if (state_id == emotion.state) return emotion.event;
  }
  return "system.ready";
}

bool pet_expression_from_theme_state_id(const std::string &state_id,
                                        lunaredu::presentation::Expression &expression) {
  if (state_id == "idle") {
    expression = lunaredu::presentation::Expression::Idle;
  } else if (state_id == "listening") {
    expression = lunaredu::presentation::Expression::Listening;
  } else if (state_id == "thinking" || state_id == "session_thinking" ||
             state_id == "surprised" || state_id == "shocked" ||
             state_id == "confused" || state_id == "silly" ||
             state_id == "embarrassed") {
    expression = lunaredu::presentation::Expression::Curious;
  } else if (state_id == "happy" || state_id == "laughing" ||
             state_id == "funny" || state_id == "loving" ||
             state_id == "confident" || state_id == "delicious") {
    expression = lunaredu::presentation::Expression::Happy;
  } else if (state_id == "alert" || state_id == "angry") {
    expression = lunaredu::presentation::Expression::Error;
  } else if (state_id == "activating" || state_id == "connecting") {
    expression = lunaredu::presentation::Expression::Curious;
  } else if (state_id == "sleepy" || state_id == "sad" ||
             state_id == "crying") {
    expression = lunaredu::presentation::Expression::Sleepy;
  } else if (state_id == "neutral" || state_id == "winking" ||
             state_id == "relaxed") {
    expression = lunaredu::presentation::Expression::Idle;
  } else {
    return false;
  }
  return true;
}

const char *pet_conversation_phase_name(PetConversationPhase phase) {
  switch (phase) {
  case PetConversationPhase::Disabled:
    return "disabled";
  case PetConversationPhase::Activating:
    return "activating";
  case PetConversationPhase::Connecting:
    return "connecting";
  case PetConversationPhase::Ready:
    return "ready";
  case PetConversationPhase::Listening:
    return "listening";
  case PetConversationPhase::Thinking:
    return "thinking";
  case PetConversationPhase::Speaking:
    return "speaking";
  case PetConversationPhase::Suspended:
    return "suspended";
  case PetConversationPhase::Failed:
    return "failed";
  }
  return "disabled";
}

}
