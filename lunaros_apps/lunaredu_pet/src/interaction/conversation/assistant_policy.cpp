#include "lunaredu_pet_conversation_policy.h"

#include <atomic>
#include "lunaredu_lunaros/app/audio_focus.h"
#include "interaction/voice/assistant_audio.h"
#include <mutex>


#include "interaction/conversation/assistant_loop.h"
#include "lunaredu_lunaros/kernel/log.h"
#include "lunaredu_pet_conversation.h"
#include "lunaredu_lunaros/persistence/settings.h"


namespace lunaredu::pet::conversation {
namespace {

constexpr const char *kTag = "lunar_os.assistant";
constexpr const char *kSettingsNamespace = "lunar_os";
constexpr const char *kAssistantEnabledKey = "assistant";
constexpr const char *kMicrophoneEnabledKey = "assistant_microphone";

std::mutex g_mutex;
AssistantPolicySnapshot g_policy;
std::atomic<bool> g_microphone_enabled{true};

bool audio_transition(void *, bool suspended, std::string &error) {
  if (!lunaredu::pet::conversation::set_suspension(
          SuspensionReason::AudioFocus, suspended)) {
    error = "assistant_suspension_failed";
    return false;
  }
  if (suspended) internal::quiesce_audio();
  return true;
}

} // namespace

void setup_assistant_policy() {
  std::lock_guard<std::mutex> lock(g_mutex);
  std::string error;
  if (!lunaredu::lunaros::audio_focus().bind_resident(audio_transition, nullptr, error))
    LOGE(kTag, "audio resident binding failed: %s", error.c_str());
  g_policy = {};
  g_policy.desired_enabled = lunaredu::lunaros::settings_get_bool(
      kSettingsNamespace, kAssistantEnabledKey, true);
  g_policy.microphone_enabled = lunaredu::lunaros::settings_get_bool(
      kSettingsNamespace, kMicrophoneEnabledKey, true);
  g_microphone_enabled.store(g_policy.microphone_enabled,
                             std::memory_order_release);

}

bool assistant_desired_enabled() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_policy.desired_enabled;
}

bool assistant_microphone_enabled() {
  return g_microphone_enabled.load(std::memory_order_acquire);
}

bool set_assistant_desired_enabled(bool enabled) {
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_policy.desired_enabled = enabled;
  }

  const auto status = lunaredu::pet::conversation::snapshot();
  if (!status.initialized) {
    on_assistant_enabled_changed(nullptr, enabled);
    return true;
  }
  if (status.enabled != enabled) {
    // The service callback persists the changed user intent exactly once.
    return lunaredu::pet::conversation::set_enabled(enabled);
  }
  on_assistant_enabled_changed(nullptr, enabled);
  return true;
}

void on_assistant_enabled_changed(void *, bool enabled) {
  (void)lunaredu::lunaros::settings_set_bool(
      kSettingsNamespace, kAssistantEnabledKey, enabled);
  std::lock_guard<std::mutex> lock(g_mutex);
  g_policy.desired_enabled = enabled;
}

bool set_assistant_microphone_enabled(bool enabled) {
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_policy.microphone_enabled = enabled;
  }
  g_microphone_enabled.store(enabled, std::memory_order_release);
  // Privacy takes effect immediately even if the preference cannot be saved.
  lunaredu::pet::conversation::internal::apply_microphone_policy_change(enabled);
  return lunaredu::lunaros::settings_set_bool(
      kSettingsNamespace, kMicrophoneEnabledKey, enabled);
}

bool request_assistant_audio_focus(void *, const char *, uint32_t) {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_policy.desired_enabled && g_policy.microphone_enabled &&
         lunaredu::lunaros::audio_focus().snapshot().owner.empty();
}

AssistantPolicySnapshot assistant_policy_snapshot() {
  std::lock_guard<std::mutex> lock(g_mutex);
  auto result = g_policy;
  result.audio_focus_owner = lunaredu::lunaros::audio_focus().snapshot().owner;
  result.temporarily_suspended = !result.audio_focus_owner.empty();
  if (result.temporarily_suspended) result.suspension_reason = "audio_focus:" + result.audio_focus_owner;
  return result;
}

} // namespace lunaredu::pet::conversation
