#pragma once

#include "lunaredu_pet_snapshot.h"
#include <string>

namespace lunaredu::pet::appearance {

inline constexpr uint32_t kTemporaryOverrideMs = 5U * 60U * 1000U;
inline constexpr uint32_t kAutonomousMoodDwellMs = 15U * 1000U;
inline constexpr uint32_t kAutonomousChangeCooldownMs = 60U * 1000U;

struct RuntimeState {
  PetMood autonomous_mood = PetMood::Neutral;
  PetMood candidate_mood = PetMood::Neutral;
  uint32_t candidate_since_ms = 0;
  uint32_t last_autonomous_change_ms = 0;
  bool initialized = false;
};

struct Update {
  PetAppearanceMode mode = PetAppearanceMode::Temporary;
  bool mode_explicit = false;
  std::string expression;
  std::string shape;
  std::string color;
};

bool valid_shape(const std::string &shape);
bool valid_color(const std::string &color);
bool valid_expression(const std::string &expression);
bool apply(PetState &state, const Update &update, uint32_t now_ms,
           std::string &error);
void resolve(PetState &state, RuntimeState &runtime, uint32_t now_ms,
             std::string &shape, std::string &color, bool autonomous_enabled = true);

} // namespace lunaredu::pet::appearance
