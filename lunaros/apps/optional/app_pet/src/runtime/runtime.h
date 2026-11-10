#pragma once
#include "lunaredu_pet_service.h"
#include "behavior/appearance_policy.h"
#include "behavior/behavior_state.h"
#include "runtime/events.h"
#include "state/pet_state.h"
#include <mutex>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
namespace lunaredu::pet::runtime {
struct Runtime {
  // Serializes appearance persistence with activation without holding mutex
  // while flash commits are in progress.
  std::mutex appearance_write_mutex;
  std::mutex mutex;
  QueueHandle_t queue = nullptr;
  BehaviorPolicy policy;
  PetEnvironmentSnapshot environment;
  std::atomic<PetConversationPhase> conversation_phase{PetConversationPhase::Disabled};
  std::string activity_state;
  bool background_working=false;
  behavior::State behavior;
  appearance::RuntimeState appearance_runtime;
  PetSnapshot snapshot;
  std::string transient_theme_state = "idle";
  uint32_t transient_until_ms = 0;
  bool transient_active = false;
  uint64_t appearance_activity_lease=0,appearance_activity_generation=0;
  uint32_t appearance_activity_until_ms=0;
  bool appearance_activity_retained=false;
  std::string activity_shape,activity_color,activity_expression;
  bool preferences_loaded = false;
  uint8_t logged_boot_progress = 255;
  bool logged_boot_complete = false;
  uint32_t last_refresh_ms = 0;
  bool environment_dirty = false;
};
extern Runtime g_runtime;
uint32_t now_ms();
void refresh_snapshot_locked(uint32_t now);
bool update_appearance(const appearance::Update &update, std::string &error);
} // namespace lunaredu::pet::runtime
