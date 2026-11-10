#include "runtime/conversation_state.h"
#include "behavior/affect_model.h"
#include "behavior/appearance_policy.h"
#include "lunaredu_pet_service.h"
#include "runtime/runtime.h"
#include "lunaredu_pet_interaction.h"
#include "lunaredu_pet_application.h"
#include "state/pet_preferences.h"
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
constexpr uint32_t kIdleRefreshIntervalMs = 100;
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
  if (!reaction.accepted) { g_runtime.snapshot.last_event = "emotion_ignored:" + emotion; return false; }
  if (assistant) g_runtime.snapshot.last_event = "assistant_emotion:" + emotion;
  else {
    g_runtime.transient_theme_state = reaction.theme;
    g_runtime.transient_until_ms = now + kEmotionHoldMs;
    g_runtime.transient_active = true;
  }
  (void)affect::apply_theme(g_runtime.snapshot.pet, reaction.theme, now);
  return true;
}
bool start_assistant_emotion_locked(const std::string &emotion, uint32_t now) {
  if (!g_runtime.policy.mood_enabled) return false;
  return record_reaction(emotion, behavior::observe_emotion(g_runtime.behavior, emotion, now), true, now);
}
bool start_emotion_reaction_locked(const std::string &emotion, uint32_t now) {
  if (!g_runtime.policy.mood_enabled) return false;
  return record_reaction(emotion, behavior::react(emotion), false, now);
}
bool start_touch_reaction_locked(const std::string &emotion, uint32_t now) {
  const auto reaction = behavior::react(emotion);
  if (!reaction.accepted) return false;
  if (g_runtime.policy.mood_enabled) {
    (void)affect::apply_theme(g_runtime.snapshot.pet, reaction.theme, now);
  }
  g_runtime.transient_theme_state = reaction.theme;
  g_runtime.transient_until_ms = now + kEmotionHoldMs;
  g_runtime.transient_active = true;
  return true;
}
void refresh_presentation_locked(uint32_t now) {
  if (g_runtime.policy.mood_enabled) affect::advance(g_runtime.snapshot.pet, now);
  behavior::Input input;
  input.now_ms = now;
  input.mood = g_runtime.snapshot.pet.mood;
  input.boot_progress = g_runtime.environment.boot_progress;
  input.conversation_phase = g_runtime.conversation_phase;
  input.activity_state = &g_runtime.activity_state;
  input.background_working=g_runtime.background_working;
  g_runtime.transient_active = g_runtime.transient_active &&
      deadline_pending(now, g_runtime.transient_until_ms);
  input.transient_active = g_runtime.transient_active;
  input.transient_theme = &g_runtime.transient_theme_state;
  const auto decision = behavior::evaluate(g_runtime.behavior, input);
  g_runtime.snapshot.conversation_phase = input.conversation_phase;
  g_runtime.snapshot.activity_state = g_runtime.activity_state;
  g_runtime.snapshot.boot_progress = decision.boot_progress;
  g_runtime.snapshot.boot_complete = decision.boot_complete;
  g_runtime.snapshot.transient_expression_active = decision.transient;
  std::string shape;
  std::string color;
  appearance::resolve(g_runtime.snapshot.pet, g_runtime.appearance_runtime, now, shape, color,
                      g_runtime.policy.autonomous_appearance_enabled);
  auto &presentation = g_runtime.snapshot.presentation;
  const uint8_t intensity = g_runtime.policy.mood_enabled
      ? affect::presentation_intensity_percent(g_runtime.snapshot.pet) : 20;
  // Thinking is the only conversation phase that collapses the body into a
  // compact dot. Dictating is a full-body speaking animation, so the selected
  // shape remains visible while audio is playing and returns to idle after it.
  std::string presentation_expression =
      input.conversation_phase == PetConversationPhase::Speaking &&
              !decision.transient
          ? "dictating"
          : decision.theme;
  if(g_runtime.appearance_activity_lease && (g_runtime.appearance_activity_retained || deadline_pending(now,g_runtime.appearance_activity_until_ms))) {
    shape=g_runtime.activity_shape;color=g_runtime.activity_color;
    presentation_expression=g_runtime.activity_expression;
  } else g_runtime.appearance_activity_lease=0;
  const bool appearance_changed =
      presentation.shape != shape || presentation.color != color;
  const bool expression_changed =
      presentation.expression_state != presentation_expression;
  if (expression_changed ||
      appearance_changed ||
      presentation.intensity_percent != intensity) {
    presentation.expression_state = presentation_expression;
    presentation.shape = std::move(shape);
    presentation.color = std::move(color);
    presentation.intensity_percent = intensity;
    presentation.revision += 1;
    if (appearance_changed) presentation.appearance_revision += 1;
    if (expression_changed) presentation.expression_revision += 1;
  }
  if (g_runtime.logged_boot_progress != decision.boot_progress || g_runtime.logged_boot_complete != decision.boot_complete) {
    g_runtime.logged_boot_progress = decision.boot_progress;
    g_runtime.logged_boot_complete = decision.boot_complete;
    ESP_LOGI(kTag, "boot presentation progress=%u complete=%d", static_cast<unsigned>(decision.boot_progress), decision.boot_complete);
  }
}

void apply_event_locked(const RobotEvent &event, uint32_t current_ms) {
  switch (event.kind) {
  case RobotEventKind::AssistantStatus:
    g_runtime.snapshot.last_event = event.text;
    if (event.detail[0] != '\0') {
      g_runtime.snapshot.last_event += ":";
      g_runtime.snapshot.last_event += event.detail;
    }
    if (std::strcmp(event.text, "已唤醒") == 0) {
      (void)start_emotion_reaction_locked("surprised", current_ms);
      g_runtime.snapshot.last_event = "assistant_wake";
    }
    break;
  case RobotEventKind::AssistantEmotion:
    (void)start_assistant_emotion_locked(event.text, current_ms);
    break;
  case RobotEventKind::ConversationTurn:
    if (g_runtime.policy.mood_enabled)
      affect::observe_conversation_turn(g_runtime.snapshot.pet, current_ms);
    g_runtime.snapshot.last_event = "conversation_turn";
    break;
  case RobotEventKind::ActivityPhase:
    if (g_runtime.conversation_phase == PetConversationPhase::Thinking) {
      const char *state = behavior::task_state_for_activity_phase(event.text);
      if (state != nullptr) {
        g_runtime.activity_state = state;
        g_runtime.snapshot.last_event = std::string("activity_phase:") + event.text;
      }
    }
    break;
  case RobotEventKind::Touch: {
    const std::string emotion =
        event.detail[0] == '\0' ? "happy" : event.detail;
    g_runtime.snapshot.last_event = "touch:" + emotion;
    (void)start_touch_reaction_locked(emotion, current_ms);
    ESP_LOGI(kTag, "touch reaction emotion=%s", emotion.c_str());
    break;
  }
  }
}

void refresh_snapshot_locked(uint32_t now) {
  refresh_presentation_locked(now);
  g_runtime.last_refresh_ms = now;
  g_runtime.environment_dirty = false;
}

bool update_appearance(const appearance::Update &update, std::string &error) {
  const uint32_t now = now_ms();
  std::lock_guard<std::mutex> appearance_lock(g_runtime.appearance_write_mutex);
  PetState candidate;
  {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    if (!g_runtime.snapshot.active) {
      error = "pet_runtime_inactive";
      return false;
    }
    candidate = g_runtime.snapshot.pet;
  }
  if (!appearance::apply(candidate, update, now, error)) return false;
  const bool appearance_changed = update.mode_explicit ||
      !update.shape.empty() || !update.color.empty();
  if (appearance_changed &&
      !state::persist_appearance_preferences(candidate)) {
    error = "pet_appearance_persist_failed";
    return false;
  }
  std::lock_guard<std::mutex> lock(g_runtime.mutex);
  if (appearance_changed) g_runtime.snapshot.pet.appearance = std::move(candidate.appearance);
  if (!update.expression.empty()) {
    g_runtime.transient_theme_state = update.expression;
    g_runtime.transient_until_ms = now + 5000U;
    g_runtime.transient_active = true;
  }
  if(g_runtime.appearance_activity_retained){g_runtime.appearance_activity_lease=0;g_runtime.appearance_activity_retained=false;}
  g_runtime.snapshot.last_event = "appearance_updated";
  refresh_snapshot_locked(now);
  error.clear();
  return true;
}

} // namespace runtime

using namespace runtime;

bool set_activity_appearance(uint64_t &lease,const char *shape,const char *color,
    const char *expression,uint32_t duration,std::string &error,bool retain_until_interaction) {
  if(!shape||!color||!expression||!appearance::valid_shape(shape)||!appearance::valid_color(color)||
      !appearance::valid_expression(expression)||duration>600000||
      (retain_until_interaction?duration!=0:duration<1)){error="pet_activity_appearance_invalid";return false;}
  std::lock_guard<std::mutex> lock(g_runtime.mutex);
  if(!g_runtime.snapshot.active){error="pet_runtime_inactive";return false;}
  const auto now=now_ms();
  // A completed display cannot claim a conversation or a newer appearance.
  if(retain_until_interaction &&
      ((g_runtime.conversation_phase!=PetConversationPhase::Ready && g_runtime.conversation_phase!=PetConversationPhase::Disabled) ||
       (g_runtime.appearance_activity_lease && g_runtime.appearance_activity_lease!=lease))){
    if(lease && g_runtime.appearance_activity_lease==lease)g_runtime.appearance_activity_lease=0;
    refresh_snapshot_locked(now);error.clear();return true;
  }
  if(g_runtime.appearance_activity_lease && !g_runtime.appearance_activity_retained && deadline_pending(now,g_runtime.appearance_activity_until_ms) &&
      g_runtime.appearance_activity_lease!=lease){error="pet_activity_appearance_busy";return false;}
  if(!lease)lease=++g_runtime.appearance_activity_generation;
  g_runtime.appearance_activity_lease=lease;g_runtime.appearance_activity_until_ms=now+duration;
  g_runtime.appearance_activity_retained=retain_until_interaction;
  g_runtime.activity_shape=shape;g_runtime.activity_color=color;g_runtime.activity_expression=expression;
  refresh_snapshot_locked(now);error.clear();return true;
}
void clear_activity_appearance(uint64_t lease){
  std::lock_guard<std::mutex> lock(g_runtime.mutex);
  if(lease&&g_runtime.appearance_activity_lease==lease){g_runtime.appearance_activity_lease=0;refresh_snapshot_locked(now_ms());}
}
void update_background_work(bool active){
  std::lock_guard<std::mutex> lock(g_runtime.mutex);
  if(g_runtime.background_working==active)return;
  g_runtime.background_working=active;
  if(g_runtime.snapshot.active)runtime::refresh_snapshot_locked(runtime::now_ms());
}
void update_environment(const PetEnvironmentSnapshot &environment) {
  std::lock_guard<std::mutex> lock(g_runtime.mutex);
  if (g_runtime.environment.boot_progress == environment.boot_progress) return;
  g_runtime.environment = environment;
  g_runtime.environment_dirty = true;
}

void runtime::update_conversation_phase(PetConversationPhase phase) {
  if(g_runtime.conversation_phase.load(std::memory_order_acquire)==phase)return;
  std::lock_guard<std::mutex> lock(g_runtime.mutex);
  if (g_runtime.conversation_phase == phase) return;
  if(g_runtime.appearance_activity_retained && phase!=PetConversationPhase::Ready && phase!=PetConversationPhase::Disabled){
    g_runtime.appearance_activity_lease=0;g_runtime.appearance_activity_retained=false;
  }
  g_runtime.conversation_phase = phase;
  if (phase != PetConversationPhase::Thinking) g_runtime.activity_state.clear();
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
      ? RobotEventKind::AssistantEmotion
      : input.kind == InteractionEventKind::ConversationTurn
            ? RobotEventKind::ConversationTurn
            : input.kind == InteractionEventKind::ActivityPhase
                  ? RobotEventKind::ActivityPhase
            : RobotEventKind::AssistantStatus;
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
  size_t processed = 0;
  for (;processed<16 && g_runtime.queue && xQueueReceive(g_runtime.queue,&event,0)==pdTRUE;++processed)
    apply_event_locked(event,now);
  // Affect decays on a 100 ms step. Recomputing an unchanged presentation at
  // the display cadence only repeats string and policy work; events and boot
  // progress still refresh immediately.
  if (processed == 0 && !g_runtime.environment_dirty &&
      now - g_runtime.last_refresh_ms < kIdleRefreshIntervalMs) return;
  refresh_snapshot_locked(now);
}
} // namespace lunaredu::pet
