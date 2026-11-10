#include "behavior/appearance_policy.h"
#include "lunaredu_pet_catalog.h"

#include <algorithm>
#include <cctype>

namespace lunaredu::pet::appearance {
namespace {
bool deadline_pending(uint32_t now, uint32_t deadline) {
  return static_cast<int32_t>(now - deadline) < 0;
}

struct AutonomousStyle {
  const char *shape;
  const char *color;
};

AutonomousStyle style_for(PetMood mood) {
  switch (mood) {
  case PetMood::Happy: return {"bean", "#F4B942"};
  case PetMood::Curious: return {"cloud", "#1CC3B0"};
  case PetMood::Sleepy: return {"pebble", "#A97EFE"};
  case PetMood::Sad: return {"teardrop", "#2A92FE"};
  case PetMood::Angry: return {"wedge", "#FF3E51"};
  case PetMood::Neutral: return {"blob", "brown"};
  }
  return {"blob", "brown"};
}
} // namespace

bool valid_shape(const std::string &shape) {
  return catalog::character_catalog_contains(catalog::kCharacterShapes,
                                                   shape.c_str());
}

bool valid_color(const std::string &color) {
  if (catalog::character_catalog_contains(catalog::kCharacterColors,
                                               color.c_str())) return true;
  if (color.size() != 7 || color[0] != '#') return false;
  return std::all_of(color.begin() + 1, color.end(), [](unsigned char value) {
    return std::isxdigit(value) != 0;
  });
}

bool valid_expression(const std::string &expression) {
  return catalog::character_catalog_contains(catalog::kCharacterStates,
                                                   expression.c_str());
}

bool apply(PetState &state, const Update &update, uint32_t now_ms,
           std::string &error) {
  if (!update.expression.empty() && !valid_expression(update.expression)) {
    error = "invalid_pet_expression";
    return false;
  }
  if (!update.shape.empty() && !valid_shape(update.shape)) {
    error = "invalid_pet_shape";
    return false;
  }
  if (!update.color.empty() && !valid_color(update.color)) {
    error = "invalid_pet_color";
    return false;
  }
  if (update.mode == PetAppearanceMode::Autonomous &&
      (!update.shape.empty() || !update.color.empty())) {
    error = "autonomous_appearance_cannot_lock_fields";
    return false;
  }
  const bool has_appearance = !update.shape.empty() || !update.color.empty();
  if (!has_appearance && update.mode_explicit &&
      update.mode != PetAppearanceMode::Autonomous) {
    error = "appearance_mode_requires_shape_or_color";
    return false;
  }
  if (update.mode_explicit || has_appearance) {
    auto &appearance = state.appearance;
    appearance.mode = update.mode;
    if (update.mode == PetAppearanceMode::Autonomous) {
      appearance.shape.clear();
      appearance.color.clear();
      appearance.temporary_until_ms = 0;
    } else {
      if (!update.shape.empty()) appearance.shape = update.shape;
      if (!update.color.empty()) appearance.color = update.color;
      appearance.temporary_until_ms = update.mode == PetAppearanceMode::Temporary
          ? now_ms + kTemporaryOverrideMs : 0;
    }
  }
  error.clear();
  return true;
}

void resolve(PetState &state, RuntimeState &runtime, uint32_t now_ms,
             std::string &shape, std::string &color, bool autonomous_enabled) {
  auto &manual = state.appearance;
  if (!autonomous_enabled) {
    // Freeze automatic mood transitions and temporary-override expiry. Manual
    // commands still take effect; explicit autonomous resets to the neutral base.
    const auto style = style_for(PetMood::Neutral);
    shape = manual.shape.empty() ? style.shape : manual.shape.c_str();
    color = manual.color.empty() ? style.color : manual.color.c_str();
    return;
  }
  if (manual.mode == PetAppearanceMode::Temporary &&
      !deadline_pending(now_ms, manual.temporary_until_ms)) {
    manual.mode = PetAppearanceMode::Autonomous;
    manual.shape.clear();
    manual.color.clear();
    manual.temporary_until_ms = 0;
  }

  if (!runtime.initialized) {
    runtime.initialized = true;
    runtime.autonomous_mood = PetMood::Neutral;
    runtime.candidate_mood = state.mood;
    runtime.candidate_since_ms = now_ms;
    runtime.last_autonomous_change_ms = now_ms;
  }
  if (state.mood != runtime.candidate_mood) {
    runtime.candidate_mood = state.mood;
    runtime.candidate_since_ms = now_ms;
  }
  const bool candidate_mature =
      now_ms - runtime.candidate_since_ms >= kAutonomousMoodDwellMs;
  const bool cooldown_complete =
      now_ms - runtime.last_autonomous_change_ms >= kAutonomousChangeCooldownMs;
  if (runtime.candidate_mood != runtime.autonomous_mood &&
      candidate_mature && cooldown_complete) {
    runtime.autonomous_mood = runtime.candidate_mood;
    runtime.last_autonomous_change_ms = now_ms;
  }

  const auto style = style_for(runtime.autonomous_mood);
  shape = style.shape;
  color = style.color;
  if (manual.mode != PetAppearanceMode::Autonomous) {
    if (!manual.shape.empty()) shape = manual.shape;
    if (!manual.color.empty()) color = manual.color;
  }
}

} // namespace lunaredu::pet::appearance
