#include "runtime/runtime.h"
#include "lunaredu_pet_application.h"
#include <esp_log.h>
namespace lunaredu::pet {
using namespace runtime;

bool setup_runtime() {
  std::lock_guard<std::mutex> lock(g_runtime.mutex);
  std::string capability_error;
  if (!register_application_capabilities(capability_error)) return false;
  if (g_runtime.queue != nullptr) return true;
  g_runtime.queue = xQueueCreate(16, sizeof(RobotEvent));
  if (g_runtime.queue == nullptr) {
    ESP_LOGE("pet", "Pet activity event queue allocation failed");
    return false;
  }
  g_runtime.snapshot.expression = lunaredu::presentation::Expression::Idle;
  g_runtime.snapshot.theme_state = "idle";
  return true;
}

void set_runtime_active(bool active) {
  std::lock_guard<std::mutex> lock(g_runtime.mutex);
  active = active && g_runtime.queue != nullptr;
  if (g_runtime.snapshot.active == active) return;
  if (g_runtime.queue != nullptr) {
    xQueueReset(g_runtime.queue);
  }
  g_runtime.snapshot.active = active;
  if (!active) {
    g_runtime.transient_expression = lunaredu::presentation::Expression::Idle;
    g_runtime.transient_theme_state = "idle";
    g_runtime.transient_until_ms = 0;
    g_runtime.transient_active = false;
    behavior::reset_interactions(g_runtime.behavior);
    g_runtime.snapshot.expression = lunaredu::presentation::Expression::Idle;
    g_runtime.snapshot.theme_state = "idle";
    g_runtime.snapshot.transient_expression_active = false;
    g_runtime.snapshot.conversation_phase = PetConversationPhase::Disabled;
    g_runtime.snapshot.last_event = "pet_runtime_stopped";
  } else {
    g_runtime.snapshot.last_event = "pet_runtime_started";
    refresh_snapshot_locked(now_ms());
  }
}
} // namespace lunaredu::pet
