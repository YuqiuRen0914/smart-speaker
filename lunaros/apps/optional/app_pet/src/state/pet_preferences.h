#pragma once

#include "lunaredu_pet_snapshot.h"

namespace lunaredu::pet::state {
void load_appearance_preferences(PetState &state);
bool persist_appearance_preferences(const PetState &state);
} // namespace lunaredu::pet::state
