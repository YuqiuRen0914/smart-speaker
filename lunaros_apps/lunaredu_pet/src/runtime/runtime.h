#pragma once
#include "lunaredu_pet_service.h"
#include "behavior/behavior_state.h"
#include "runtime/events.h"
#include "state/pet_state.h"
#include <mutex>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
namespace lunaredu::pet::runtime {
struct Runtime {
  std::mutex mutex;
  QueueHandle_t queue = nullptr;
  PetState pet;
  PetEnvironmentSnapshot environment;
  PetConversationPhase conversation_phase = PetConversationPhase::Disabled;
  behavior::State behavior;
  PetSnapshot snapshot;
  lunaredu::presentation::Expression transient_expression = lunaredu::presentation::Expression::Idle;
  std::string transient_theme_state = "idle";
  uint32_t transient_until_ms = 0;
  bool transient_active = false;
  uint8_t logged_boot_progress = 255;
  bool logged_boot_complete = false;
};
extern Runtime g_runtime;
uint32_t now_ms();
void refresh_snapshot_locked(uint32_t now);
} // namespace lunaredu::pet::runtime
