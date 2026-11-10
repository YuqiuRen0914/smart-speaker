#pragma once
#include "lunaredu_pet_snapshot.h"
namespace lunaredu::pet::behavior {
struct State {
  bool boot_complete = false;
  bool emotion_active = false;
  std::string emotion_theme = "idle";
  uint32_t emotion_until_ms = 0;
  bool has_presented_emotion = false;
  std::string last_presented_theme;
  uint32_t last_presented_emotion_ms = 0;
};
struct Reaction {
  bool accepted = false;
  bool present = false;
  std::string theme = "idle";
};
struct Input {
  PetMood mood = PetMood::Neutral;
  uint32_t now_ms = 0;
  uint8_t boot_progress = 0;
  PetConversationPhase conversation_phase = PetConversationPhase::Disabled;
  const std::string *activity_state = nullptr;
  bool background_working=false;
  bool transient_active = false;
  const std::string *transient_theme = nullptr;
};
struct Decision {
  std::string theme = "idle";
  bool transient = false;
  uint8_t boot_progress = 0;
  bool boot_complete = false;
};
const char *task_state_for_activity_phase(const std::string &phase);
Reaction react(const std::string &);
Reaction observe_emotion(State &, const std::string &, uint32_t now_ms);
void reset_interactions(State &);
Decision evaluate(State &, const Input &);
} // namespace lunaredu::pet::behavior
