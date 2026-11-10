#pragma once
#include <cstdint>
#include <string>
#include "lunaredu_pet_types.h"

namespace lunaredu::pet {
// Product readiness is external. Conversation state is synchronized internally
// by Pet's conversation service, never assembled by a product adapter.
struct PetEnvironmentSnapshot {
  uint8_t boot_progress = 0;
};


enum class PetMood : uint8_t { Neutral, Happy, Curious, Sleepy, Sad, Angry };

// Pet-owned state. New attributes belong here when their behavior is defined.
struct PetState {
  PetMood mood = PetMood::Neutral;
};
const char *pet_mood_name(PetMood mood);

struct PetSnapshot {
  PetState pet;
  bool active = false;
  lunaredu::presentation::Expression expression = lunaredu::presentation::Expression::Idle;
  PetConversationPhase conversation_phase = PetConversationPhase::Disabled;
  uint8_t boot_progress = 0;
  bool boot_complete = false;
  std::string last_event;
  std::string last_emotion;
  // Semantic visual state selected after priority arbitration. Unlike
  // lunaredu::presentation::Expression this preserves activation, connection and reactive states
  // for a replaceable on-device theme.
  std::string theme_state = "idle";
  bool transient_expression_active = false;
};

PetSnapshot snapshot();
bool boot_complete();
void theme_state_into(std::string &state);
} // namespace lunaredu::pet
