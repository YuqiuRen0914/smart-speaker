#pragma once
#include "lunaredu_pet_snapshot.h"
namespace lunaredu::pet::state {
// Called only by the core runtime while it owns the state lock.
bool set_mood(PetState &state, PetMood mood);
} // namespace lunaredu::pet::state
