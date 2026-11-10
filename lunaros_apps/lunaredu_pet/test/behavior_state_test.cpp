#include "../src/behavior/behavior_state.h"
#include "state/pet_state.h"
#include <cassert>
#include <iostream>
int main() {
  using namespace lunaredu::pet;
  behavior::State state;
  behavior::Input input;
  assert(behavior::evaluate(state, input).expression == lunaredu::presentation::Expression::Boot);
  input.boot_progress = 100;
  input.conversation_phase = PetConversationPhase::Speaking;
  assert(behavior::evaluate(state, input).expression == lunaredu::presentation::Expression::Idle);
  assert(behavior::observe_emotion(state, "happy", 100).accepted);
  input.now_ms = 6000;
  auto decision = behavior::evaluate(state, input);
  assert(decision.expression == lunaredu::presentation::Expression::Happy && decision.transient);
  input.conversation_phase = PetConversationPhase::Ready;
  assert(behavior::evaluate(state, input).expression == lunaredu::presentation::Expression::Idle);
  assert(!behavior::observe_emotion(state, "unsupported_xyz", 6000).accepted);
  assert(behavior::observe_emotion(state, "happy", 6000).accepted);
  behavior::reset_interactions(state);
  input.boot_progress = 20;
  assert(behavior::evaluate(state, input).boot_complete);
  input.conversation_phase = PetConversationPhase::Listening;
  assert(behavior::evaluate(state, input).expression == lunaredu::presentation::Expression::Listening);
  assert(behavior::react("laughing").theme == "laughing");
  // Hold expiry remains correct across the 32-bit millisecond wrap.
  behavior::observe_emotion(state, "happy", 0xfffffff0U);
  input.conversation_phase = PetConversationPhase::Ready;
  input.now_ms = 10;
  assert(behavior::evaluate(state, input).expression == lunaredu::presentation::Expression::Happy);
  input.now_ms = 3000;
  assert(behavior::evaluate(state, input).expression == lunaredu::presentation::Expression::Idle);
  PetState pet;
  PetMood mood = PetMood::Neutral;
  assert(behavior::mood_from_emotion("happy", mood));
  assert(state::set_mood(pet, mood));
  assert(pet.mood == PetMood::Happy);
  assert(!state::set_mood(pet, static_cast<PetMood>(255)));
  assert(pet.mood == PetMood::Happy);
  assert(!behavior::mood_from_emotion("connecting", mood));
  assert(!behavior::mood_from_emotion("listening", mood));
  assert(!behavior::mood_from_emotion("error", mood));
  input.mood = pet.mood;
  assert(behavior::evaluate(state, input).expression == lunaredu::presentation::Expression::Happy);
  input.conversation_phase = PetConversationPhase::Listening;
  assert(behavior::evaluate(state, input).expression == lunaredu::presentation::Expression::Listening);
  input.conversation_phase = PetConversationPhase::Ready;
  decision = behavior::evaluate(state, input);
  assert(decision.expression == lunaredu::presentation::Expression::Happy && decision.theme == "happy");
  assert(pet.mood == PetMood::Happy);
  std::cout << "Pet mood ownership / behavior input / decision checks passed\n";
}
