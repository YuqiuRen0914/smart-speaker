#pragma once
#include "lunaredu_pet_interaction.h"
#include "lunaredu_pet_snapshot.h"
namespace lunaredu::pet {
bool setup_runtime();
// Idempotent. Deactivation cancels queued/transient interactions, preserves
// mood and readiness, and publishes an idle snapshot. Inactive intake rejects
// events. Conversation service enablement is controlled separately.
void set_runtime_active(bool active);
void update_environment(const PetEnvironmentSnapshot &environment);
void tick_runtime();
}
