#pragma once

#include "smart_speaker_character.h"
#include "../product/desktop_resources.h"

namespace smart_speaker {

enum class AssistantVisualState : uint8_t {
  Inactive, Listening, Thinking, Speaking, Failed,
  Happy, Curious, Sleepy, Error,
};

inline product::PetFrame pet_frame_for_state(character::Mood mood,
    AssistantVisualState assistant, uint32_t now_ms) {
  switch (assistant) {
    case AssistantVisualState::Listening:
    case AssistantVisualState::Thinking:
      return product::PetFrame::Think;
    case AssistantVisualState::Speaking:
      return product::PetFrame::Talk;
    case AssistantVisualState::Failed:
    case AssistantVisualState::Error:
      return product::PetFrame::Angry;
    case AssistantVisualState::Happy:
      return product::PetFrame::Happy;
    case AssistantVisualState::Curious:
      return product::PetFrame::Think;
    case AssistantVisualState::Sleepy:
      return product::PetFrame::Idle;
    case AssistantVisualState::Inactive:
      break;
  }
  switch (mood) {
    case character::Mood::Happy:
    case character::Mood::Excited:
    case character::Mood::Surprised:
      return product::PetFrame::Happy;
    case character::Mood::Confused:
      return product::PetFrame::Angry;
    case character::Mood::Progress:
    case character::Mood::Radar:
    case character::Mood::Listening:
      return product::PetFrame::Think;
    case character::Mood::Humming:
      return product::PetFrame::Talk;
    case character::Mood::Playful:
      return static_cast<product::PetFrame>(
          static_cast<uint8_t>(product::PetFrame::Poke0) + (now_ms / 150U) % 4U);
    case character::Mood::Idle:
    case character::Mood::Curious:
    case character::Mood::Drowsy:
    case character::Mood::Sleeping:
    default:
      return product::PetFrame::Idle;
  }
}

} // namespace smart_speaker
