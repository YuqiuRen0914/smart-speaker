#pragma once

#include "lunaredu_pet_snapshot.h"
#include <string_view>

namespace lunaredu::pet::affect {

struct Personality {
  float baseline_valence = 0.08f;
  float baseline_arousal = 0.32f;
  float baseline_affinity = 0.20f;
  float baseline_energy = 0.68f;
  float responsiveness = 0.72f;
  float expressiveness = 0.78f;
};

struct Delta {
  float valence = 0.0f;
  float arousal = 0.0f;
  float affinity = 0.0f;
  float energy = 0.0f;
  float intensity = 0.0f;
};

const Personality &default_personality();
bool delta_from_theme(std::string_view theme, Delta &delta);
void advance(PetState &state, uint32_t now_ms,
             const Personality &personality = default_personality());
bool apply_theme(PetState &state, std::string_view theme, uint32_t now_ms,
                 float confidence = 1.0f,
                 const Personality &personality = default_personality());
void observe_conversation_turn(
    PetState &state, uint32_t now_ms,
    const Personality &personality = default_personality());
uint8_t presentation_intensity_percent(
    const PetState &state,
    const Personality &personality = default_personality());

} // namespace lunaredu::pet::affect
