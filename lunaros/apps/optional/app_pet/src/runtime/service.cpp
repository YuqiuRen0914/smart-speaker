#include "runtime/runtime.h"
#include "lunaredu_pet_application.h"
#include "state/pet_preferences.h"
#include "runtime/conversation_state.h"
#include "character/project.h"
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <freertos/idf_additions.h>
#include <sdkconfig.h>
namespace lunaredu::pet {
using namespace runtime;

void configure_behavior_policy(const BehaviorPolicy &policy) {
  std::lock_guard<std::mutex> lock(g_runtime.mutex);
  g_runtime.policy = policy;
  if (!policy.mood_enabled) {
    behavior::reset_interactions(g_runtime.behavior);
    g_runtime.snapshot.pet.mood = PetMood::Neutral;
    g_runtime.transient_active = false;
  }
  if (g_runtime.snapshot.active) refresh_snapshot_locked(now_ms());
}

bool setup_runtime() {
  character::restore();
  std::lock_guard<std::mutex> lock(g_runtime.mutex);

  if (!g_runtime.preferences_loaded) {
    state::load_appearance_preferences(g_runtime.snapshot.pet);
    g_runtime.preferences_loaded = true;
  }
  if (g_runtime.queue != nullptr) return true;
  // Events are consumed by ordinary tasks, never an ISR. Keep their storage
  // out of the internal RAM needed by radio, DMA and executor stacks.
#if CONFIG_SPIRAM
  constexpr unsigned queue_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
#else
  constexpr unsigned queue_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
#endif
  g_runtime.queue = xQueueCreateWithCaps(16, sizeof(RobotEvent), queue_caps);
  if (g_runtime.queue == nullptr) {
    ESP_LOGE("pet", "Pet activity event queue allocation failed");
    return false;
  }
  return true;
}

void set_runtime_active(bool active) {
  std::lock_guard<std::mutex> appearance_lock(g_runtime.appearance_write_mutex);
  std::lock_guard<std::mutex> lock(g_runtime.mutex);
  active = active && g_runtime.queue != nullptr;
  if (g_runtime.snapshot.active == active) return;
  if (g_runtime.queue != nullptr) {
    xQueueReset(g_runtime.queue);
  }
  g_runtime.snapshot.active = active;
  if (!active) {
    g_runtime.activity_state.clear();
    g_runtime.snapshot.activity_state.clear();
    g_runtime.transient_theme_state = "idle";
    g_runtime.transient_until_ms = 0;
    g_runtime.transient_active = false;
    behavior::reset_interactions(g_runtime.behavior);
    auto &presentation = g_runtime.snapshot.presentation;
    if (presentation.expression_state != "idle") {
      presentation.expression_state = "idle";
      ++presentation.expression_revision;
      ++presentation.revision;
    }
    g_runtime.snapshot.transient_expression_active = false;
    g_runtime.appearance_activity_lease=0;
    g_runtime.snapshot.conversation_phase = PetConversationPhase::Disabled;
    g_runtime.snapshot.last_event = "pet_runtime_stopped";
  } else {
    g_runtime.snapshot.last_event = "pet_runtime_started";
    refresh_snapshot_locked(now_ms());
  }
}

void update_conversation_phase(PetConversationPhase phase) {
  runtime::update_conversation_phase(phase);
}
} // namespace lunaredu::pet
