#pragma once
#include "lunaredu_pet_snapshot.h"
namespace lunaredu::pet::behavior {
struct State {
  bool boot_complete = false;
  bool emotion_active = false;
  lunaredu::presentation::Expression emotion_expression = lunaredu::presentation::Expression::Idle;
  std::string emotion_theme = "idle";
  uint32_t emotion_until_ms = 0;
};
struct Reaction {
  bool accepted = false;
  lunaredu::presentation::Expression expression = lunaredu::presentation::Expression::Idle;
  std::string theme = "idle";
};
struct Input {
  PetMood mood = PetMood::Neutral;
  uint32_t now_ms = 0;
  uint8_t boot_progress = 0;
  PetConversationPhase conversation_phase = PetConversationPhase::Disabled;
  bool transient_active = false;
  lunaredu::presentation::Expression transient_expression = lunaredu::presentation::Expression::Idle;
  const std::string *transient_theme = nullptr;
};
struct Decision {
  lunaredu::presentation::Expression expression = lunaredu::presentation::Expression::Idle;
  std::string theme = "idle";
  bool transient = false;
  uint8_t boot_progress = 0;
  bool boot_complete = false;
};
bool mood_from_emotion(const std::string &, PetMood &);
Reaction react(const std::string &);
Reaction observe_emotion(State &, const std::string &, uint32_t now_ms);
void reset_interactions(State &);
Decision evaluate(State &, const Input &);
} // namespace lunaredu::pet::behavior
