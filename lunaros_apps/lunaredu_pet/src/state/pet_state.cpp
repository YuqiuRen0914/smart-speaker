#include "state/pet_state.h"
namespace lunaredu::pet {
const char *pet_mood_name(PetMood mood) {
  switch (mood) {
  case PetMood::Neutral: return "neutral";
  case PetMood::Happy: return "happy";
  case PetMood::Curious: return "curious";
  case PetMood::Sleepy: return "sleepy";
  case PetMood::Sad: return "sad";
  case PetMood::Angry: return "angry";
  }
  return "neutral";
}
namespace state {
bool set_mood(PetState &state, PetMood mood) {
  switch (mood) {
  case PetMood::Neutral: case PetMood::Happy: case PetMood::Curious:
  case PetMood::Sleepy: case PetMood::Sad: case PetMood::Angry:
    state.mood = mood;
    return true;
  }
  return false;
}
} // namespace state
} // namespace lunaredu::pet
