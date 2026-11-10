#pragma once
#include <cstdint>
#include <string>
#include "lunaredu_pet_types.h"

namespace lunaredu::pet {
// Product readiness is external. Product adapters project optional conversation
// signals through Pet's public contracts; Pet owns their visual interpretation.
struct PetEnvironmentSnapshot {
  uint8_t boot_progress = 0;
};


enum class PetMood : uint8_t { Neutral, Happy, Curious, Sleepy, Sad, Angry };

enum class PetAppearanceMode : uint8_t { Autonomous, Temporary, Locked };

// Slowly changing affect owned by Pet.  The renderer never mutates these
// values; conversation and interaction events contribute bounded deltas and
// the model returns toward its personality baseline over time.
struct PetAffectState {
  float valence = 0.08f;  // -1 negative, +1 positive
  float arousal = 0.32f;  // 0 calm, 1 excited
  float affinity = 0.20f; // -1 distant, +1 attached
  float energy = 0.68f;   // 0 exhausted, 1 energetic
  float reaction_intensity = 0.0f;
  uint32_t conversation_turns = 0;
  uint32_t last_update_ms = 0;
  bool initialized = false;
};

// Manual appearance is an input to presentation arbitration, not a direct
// renderer command. Temporary overrides expire; locked preferences survive a
// reboot; autonomous mode releases both fields back to the affect model.
struct PetAppearanceState {
  PetAppearanceMode mode = PetAppearanceMode::Autonomous;
  std::string shape;
  std::string color;
  uint32_t temporary_until_ms = 0;
};

struct PetPresentationIntent {
  std::string expression_state = "idle";
  std::string shape = "blob";
  std::string color = "brown";
  uint8_t intensity_percent = 20;
  uint32_t revision = 0;
  uint32_t appearance_revision = 0;
  uint32_t expression_revision = 0;
};

// Pet-owned state. New attributes belong here when their behavior is defined.
struct PetState {
  PetMood mood = PetMood::Neutral;
  PetAffectState affect;
  PetAppearanceState appearance;
};
const char *pet_mood_name(PetMood mood);
const char *pet_appearance_mode_name(PetAppearanceMode mode);

struct PetSnapshot {
  PetState pet;
  bool active = false;
  PetConversationPhase conversation_phase = PetConversationPhase::Disabled;
  uint8_t boot_progress = 0;
  bool boot_complete = false;
  std::string last_event;
  // Real activity phase, independent of mood and manual appearance.
  std::string activity_state;
  bool transient_expression_active = false;
  PetPresentationIntent presentation;
  bool mood_enabled = true;
  bool autonomous_appearance_enabled = true;
};

PetSnapshot snapshot();
// The foreground copies only the fields it draws. Strings are copied only
// when their respective revision differs from the last applied revision.
struct PetVisualSnapshot {
  PetConversationPhase conversation_phase = PetConversationPhase::Disabled;
  bool boot_complete = false;
  std::string expression_state;
  uint32_t expression_revision = 0;
  uint32_t appearance_revision = 0;
  std::string shape;
  std::string color;
};
PetVisualSnapshot visual_snapshot(uint32_t applied_appearance_revision,
                                  uint32_t applied_expression_revision = ~uint32_t{0});
bool boot_complete();
} // namespace lunaredu::pet
