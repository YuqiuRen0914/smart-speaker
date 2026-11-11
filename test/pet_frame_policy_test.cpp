#include "smart_speaker_pet_frame_policy.h"
#include <cassert>
#include <iostream>

using namespace smart_speaker;
int main() {
  using M = character::Mood;
  using A = AssistantVisualState;
  using F = product::PetFrame;
  assert(pet_frame_for_state(M::Idle, A::Listening, 0) == F::Think);
  assert(pet_frame_for_state(M::Happy, A::Thinking, 0) == F::Think);
  assert(pet_frame_for_state(M::Idle, A::Speaking, 0) == F::Talk);
  assert(pet_frame_for_state(M::Idle, A::Failed, 0) == F::Angry);
  assert(pet_frame_for_state(M::Idle, A::Happy, 0) == F::Happy);
  assert(pet_frame_for_state(M::Idle, A::Curious, 0) == F::Think);
  assert(pet_frame_for_state(M::Idle, A::Sleepy, 0) == F::Idle);
  assert(pet_frame_for_state(M::Happy, A::Inactive, 0) == F::Happy);
  assert(pet_frame_for_state(M::Playful, A::Inactive, 0) == F::Poke0);
  assert(pet_frame_for_state(M::Playful, A::Inactive, 450) == F::Poke3);
  assert(pet_frame_for_state(M::Playful, A::Inactive, 600) == F::Poke0);
  std::cout << "BigFish frame policy: assistant phases, emotions, mood fallback and bounded interaction loop passed\n";
}
