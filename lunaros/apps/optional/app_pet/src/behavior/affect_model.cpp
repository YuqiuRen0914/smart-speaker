#include "behavior/affect_model.h"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace lunaredu::pet::affect {
namespace {
float clamp_unit(float value) { return std::clamp(value, 0.0f, 1.0f); }
float clamp_signed(float value) { return std::clamp(value, -1.0f, 1.0f); }
bool contains(std::string_view value, std::string_view needle) {
  return value.find(needle) != std::string_view::npos;
}

PetMood classify(const PetAffectState &state) {
  if (state.energy < 0.27f && state.arousal < 0.48f) return PetMood::Sleepy;
  if (state.valence < -0.28f && state.arousal > 0.55f) return PetMood::Angry;
  if (state.valence < -0.17f) return PetMood::Sad;
  if (state.valence > 0.25f) return PetMood::Happy;
  if (state.arousal > 0.51f) return PetMood::Curious;
  return PetMood::Neutral;
}

void apply_delta(PetState &state, const Delta &delta, float scale) {
  auto &value = state.affect;
  value.valence = clamp_signed(value.valence + delta.valence * scale);
  value.arousal = clamp_unit(value.arousal + delta.arousal * scale);
  value.affinity = clamp_signed(value.affinity + delta.affinity * scale);
  value.energy = clamp_unit(value.energy + delta.energy * scale);
  value.reaction_intensity =
      std::max(value.reaction_intensity, clamp_unit(delta.intensity * scale));
  state.mood = classify(value);
}
} // namespace

const Personality &default_personality() {
  static constexpr Personality value{};
  return value;
}

bool delta_from_theme(std::string_view theme, Delta &delta) {
  delta = {};
  if (theme == "happy") delta = Delta{.27f,.14f,.025f,.04f,.72f};
  else if (theme == "excited") delta = Delta{.32f,.30f,.03f,.05f,.85f};
  else if (theme == "laughing") delta = Delta{.34f,.25f,.035f,.05f,.90f};
  else if (theme == "celebrate") delta = Delta{.34f,.30f,.035f,.05f,.90f};
  else if (theme == "loving") delta = Delta{.31f,.05f,.085f,.02f,.80f};
  else if (theme == "funny" || theme == "silly") delta = Delta{.25f,.28f,.025f,.04f,.84f};
  else if (theme == "confident") delta = Delta{.20f,.12f,.018f,.05f,.68f};
  else if (theme == "delicious") delta = Delta{.24f,.10f,.02f,.04f,.66f};
  else if (theme == "sad") delta = Delta{-.22f,-.06f,.012f,-.07f,.62f};
  else if (theme == "crying") delta = Delta{-.31f,.08f,.018f,-.10f,.80f};
  else if (theme == "angry") delta = Delta{-.34f,.31f,-.025f,.02f,.86f};
  else if (theme == "shocked") delta = Delta{-.08f,.38f,0.0f,.02f,.92f};
  else if (theme == "surprised") delta = Delta{.05f,.34f,0.0f,.025f,.85f};
  else if (theme == "confused" || theme == "thinking") delta = Delta{-.025f,.13f,0.0f,-.015f,.58f};
  else if (theme == "embarrassed") delta = Delta{-.04f,.16f,.018f,-.015f,.64f};
  else if (theme == "sleepy") delta = Delta{0.0f,-.28f,0.0f,-.22f,.52f};
  else if (theme == "relaxed") delta = Delta{.09f,-.20f,0.0f,.025f,.40f};
  else if (theme == "winking") delta = Delta{.13f,.11f,.025f,0.0f,.61f};
  else if (theme == "neutral") delta = Delta{0.0f,-.035f,0.0f,0.0f,.20f};
  else if (contains(theme, "alert")) delta = Delta{-.18f,.24f,0.0f,-.02f,.72f};
  else return false;
  return true;
}

void advance(PetState &state, uint32_t now_ms, const Personality &personality) {
  auto &value = state.affect;
  if (!value.initialized) {
    value.last_update_ms = now_ms;
    value.initialized = true;
    state.mood = classify(value);
    return;
  }
  const uint32_t elapsed_ms = now_ms - value.last_update_ms;
  if (elapsed_ms < 100U) return;
  value.last_update_ms = now_ms;
  const float seconds = std::min(static_cast<float>(elapsed_ms) / 1000.0f, 60.0f);
  const auto recover = [seconds](float current, float baseline, float tau_seconds) {
    const float amount = 1.0f - std::exp(-seconds / tau_seconds);
    return current + (baseline - current) * amount;
  };
  value.valence = recover(value.valence, personality.baseline_valence, 210.0f);
  value.arousal = recover(value.arousal, personality.baseline_arousal, 70.0f);
  value.affinity = recover(value.affinity, personality.baseline_affinity, 1800.0f);
  value.energy = recover(value.energy, personality.baseline_energy, 420.0f);
  value.reaction_intensity = recover(value.reaction_intensity, 0.0f, 7.0f);
  state.mood = classify(value);
}

bool apply_theme(PetState &state, std::string_view theme, uint32_t now_ms,
                 float confidence, const Personality &personality) {
  Delta delta;
  if (!delta_from_theme(theme, delta)) return false;
  advance(state, now_ms, personality);
  const float affinity_sensitivity = 1.0f + std::max(0.0f, state.affect.affinity) * 0.18f;
  const float scale = personality.responsiveness *
      std::clamp(confidence, 0.0f, 1.0f) * affinity_sensitivity;
  apply_delta(state, delta, scale);
  return true;
}

void observe_conversation_turn(PetState &state, uint32_t now_ms,
                               const Personality &personality) {
  advance(state, now_ms, personality);
  // Every completed user utterance matters, even when no semantic emotion is
  // supplied by the server. Engagement raises affinity and alertness gently;
  // the later structured LLM emotion supplies the directional response.
  state.affect.conversation_turns += 1;
  apply_delta(state, Delta{0.0f,.025f,.006f,-.003f,.16f},
              personality.responsiveness);
}

uint8_t presentation_intensity_percent(const PetState &state,
                                       const Personality &personality) {
  const auto &value = state.affect;
  const float persistent = std::max(std::abs(value.valence),
                                    std::abs(value.arousal - personality.baseline_arousal));
  const float result = std::clamp(std::max(value.reaction_intensity, persistent) *
                                      personality.expressiveness,
                                  0.12f, 1.0f);
  return static_cast<uint8_t>(std::lround(result * 100.0f));
}

} // namespace lunaredu::pet::affect
