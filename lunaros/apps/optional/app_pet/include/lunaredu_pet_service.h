#pragma once
#include <string>
#include "lunaredu_pet_interaction.h"
#include "lunaredu_pet_snapshot.h"
namespace lunaredu::lunaros { struct DeviceRuntimeContext; }
namespace lunaredu::pet {
struct BehaviorPolicy {
  bool mood_enabled = true;
  bool autonomous_appearance_enabled = true;
};
// Product-owned policy; manual appearance and conversation animations remain active.
void configure_behavior_policy(const BehaviorPolicy &policy);
bool setup_runtime();
// Service-owner transition: runtime activity and its device projection become
// available together. Product readiness and hardware dependencies stay outside.
bool start_runtime_service(lunaros::DeviceRuntimeContext &, const BehaviorPolicy &,
                           std::string &error);
bool stop_runtime_service(lunaros::DeviceRuntimeContext &, std::string &error);
// Idempotent. Deactivation cancels queued/transient interactions, preserves
// mood and readiness, and publishes an idle snapshot. Inactive intake rejects
// events. Conversation service enablement is controlled separately.
void set_runtime_active(bool active);
void update_environment(const PetEnvironmentSnapshot &environment);
void tick_runtime();
// Product composition supplies the current assistant phase when installed.
void update_conversation_phase(PetConversationPhase phase);
}
