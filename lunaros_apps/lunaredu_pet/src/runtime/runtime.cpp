#include "runtime/conversation_state.h"
#include "lunaredu_pet_service.h"
#include "lunaredu_pet_emotion.h"
#include "runtime/runtime.h"
#include "lunaredu_pet_interaction.h"
#include "lunaredu_pet_application.h"
#include <limits>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <utility>

#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>


namespace lunaredu::pet {
namespace runtime {
constexpr uint32_t kEmotionHoldMs = 2600;
constexpr const char *kTag = "pet";
bool deadline_pending(uint32_t now, uint32_t deadline) {return static_cast<int32_t>(now-deadline)<0;}

Runtime g_runtime;

uint32_t now_ms() {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

template <size_t N>
void copy_text(char (&target)[N], const char *source) {
  if (source == nullptr) {
    target[0] = '\0';
    return;
  }
  std::strncpy(target, source, N - 1);
  target[N - 1] = '\0';
}

bool enqueue_event(const RobotEvent &event) {
  std::lock_guard<std::mutex> lock(g_runtime.mutex);
  return g_runtime.snapshot.active && g_runtime.queue != nullptr &&
      xQueueSend(g_runtime.queue, &event, 0) == pdTRUE;
}

bool record_reaction(const std::string &emotion, const behavior::Reaction &reaction, bool assistant, uint32_t now) {
  g_runtime.snapshot.last_emotion = emotion;
  if (!reaction.accepted) { g_runtime.snapshot.last_event = "emotion_ignored:" + emotion; return false; }
  if (assistant) g_runtime.snapshot.last_event = "assistant_emotion:" + emotion;
  else {
    g_runtime.transient_expression = reaction.expression;
    g_runtime.transient_theme_state = reaction.theme;
    g_runtime.transient_until_ms = now + kEmotionHoldMs;
    g_runtime.transient_active = true;
  }
  PetMood mood;
  if (behavior::mood_from_emotion(emotion, mood)) {
    (void)state::set_mood(g_runtime.pet, mood);
  }
  return true;
}
bool start_assistant_emotion_locked(const std::string &emotion, uint32_t now) {
  return record_reaction(emotion, behavior::observe_emotion(g_runtime.behavior, emotion, now), true, now);
}
bool start_emotion_reaction_locked(const std::string &emotion, uint32_t now) {
  return record_reaction(emotion, behavior::react(emotion), false, now);
}
lunaredu::presentation::Expression expression_for_locked(uint32_t now) {
  behavior::Input input;
  input.now_ms = now;
  input.mood = g_runtime.pet.mood;
  input.boot_progress = g_runtime.environment.boot_progress;
  input.conversation_phase = g_runtime.conversation_phase;
  g_runtime.transient_active = g_runtime.transient_active &&
      deadline_pending(now, g_runtime.transient_until_ms);
  input.transient_active = g_runtime.transient_active;
  input.transient_expression = g_runtime.transient_expression;
  input.transient_theme = &g_runtime.transient_theme_state;
  const auto decision = behavior::evaluate(g_runtime.behavior, input);
  g_runtime.snapshot.conversation_phase = input.conversation_phase;
  g_runtime.snapshot.boot_progress = decision.boot_progress;
  g_runtime.snapshot.boot_complete = decision.boot_complete;
  g_runtime.snapshot.theme_state = decision.theme;
  g_runtime.snapshot.transient_expression_active = decision.transient;
  if (g_runtime.logged_boot_progress != decision.boot_progress || g_runtime.logged_boot_complete != decision.boot_complete) {
    g_runtime.logged_boot_progress = decision.boot_progress;
    g_runtime.logged_boot_complete = decision.boot_complete;
    ESP_LOGI(kTag, "boot presentation progress=%u complete=%d", static_cast<unsigned>(decision.boot_progress), decision.boot_complete);
  }
  return decision.expression;
}

void apply_event_locked(const RobotEvent &event, uint32_t current_ms) {
  switch (event.kind) {
  case RobotEventKind::AssistantStatus:
    g_runtime.snapshot.last_event = event.text;
    if (event.detail[0] != '\0') {
      g_runtime.snapshot.last_event += ":";
      g_runtime.snapshot.last_event += event.detail;
    }
    break;
  case RobotEventKind::AssistantEmotion:
    (void)start_assistant_emotion_locked(event.text, current_ms);
    break;
  case RobotEventKind::Touch: {
    const std::string emotion =
        event.detail[0] == '\0' ? "happy" : event.detail;
    g_runtime.snapshot.last_event = "touch:" + emotion;
    (void)start_emotion_reaction_locked(emotion, current_ms);
    ESP_LOGI(kTag, "touch reaction emotion=%s", emotion.c_str());
    break;
  }
  }
}

void refresh_snapshot_locked(uint32_t now) {
  g_runtime.snapshot.expression = expression_for_locked(now);
}

} // namespace runtime

using namespace runtime;

void update_environment(const PetEnvironmentSnapshot &environment) {
  std::lock_guard<std::mutex> lock(g_runtime.mutex);
  g_runtime.environment = environment;
}

void runtime::update_conversation_phase(PetConversationPhase phase) {
  std::lock_guard<std::mutex> lock(g_runtime.mutex);
  if (g_runtime.conversation_phase == phase) return;
  g_runtime.conversation_phase = phase;
  if (phase != PetConversationPhase::Disabled &&
      phase != PetConversationPhase::Suspended) {
    g_runtime.transient_active = false;
    g_runtime.transient_until_ms = 0;
  }
  // Keep independent response emotions across listening/thinking/speaking.
  // An inactive Pet retains the latest phase for reactivation, while its
  // published snapshot continues to report an inactive semantic runtime.
  if (g_runtime.snapshot.active) refresh_snapshot_locked(now_ms());
}

bool post_interaction_event(const InteractionEvent &input) {
  RobotEvent event;
  event.kind = input.kind == InteractionEventKind::Emotion
      ? RobotEventKind::AssistantEmotion : RobotEventKind::AssistantStatus;
  copy_text(event.text, input.value);
  copy_text(event.detail, input.detail);
  return enqueue_event(event);
}

bool post_touch_reaction(const char *emotion) {
  RobotEvent event;
  event.kind = RobotEventKind::Touch;
  copy_text(event.detail, emotion);
  return enqueue_event(event);
}

void tick_runtime() {
  const auto now=now_ms();
  std::lock_guard<std::mutex> lock(g_runtime.mutex);
  if (!g_runtime.snapshot.active) return;
  RobotEvent event;
  for (size_t n=0;n<16 && g_runtime.queue && xQueueReceive(g_runtime.queue,&event,0)==pdTRUE;++n)
    apply_event_locked(event,now);
  refresh_snapshot_locked(now);
}
} // namespace lunaredu::pet
