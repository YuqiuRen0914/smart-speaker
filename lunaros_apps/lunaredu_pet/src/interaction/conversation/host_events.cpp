#include "interaction/conversation/host_events.h"

#include <mutex>
#include "lunaredu_pet_conversation_events.h"

namespace {

std::mutex g_ui_mutex;
lunaredu::pet::conversation::HostCallbacks g_host_callbacks;

lunaredu::pet::conversation::HostCallbacks host_callbacks_snapshot() {
  std::lock_guard<std::mutex> lock(g_ui_mutex);
  return g_host_callbacks;
}

} // namespace

void conversation_set_host_callbacks(
    const lunaredu::pet::conversation::HostCallbacks &callbacks) {
  std::lock_guard<std::mutex> lock(g_ui_mutex);
  g_host_callbacks = callbacks;
}

namespace {
void emit(const lunaredu::pet::conversation::AssistantEvent &event) {
  // Pet intake is unconditional. The host callback is an optional observer.
  (void)lunaredu::pet::post_conversation_event(event);
  const auto callbacks = host_callbacks_snapshot();
  if (callbacks.on_event) callbacks.on_event(callbacks.context, event);
}
}
void conversation_emit_status(const char *state, const char *detail,
                            const char *result, const char *error) {
  emit({.kind = lunaredu::pet::conversation::AssistantEventKind::Status,
        .value = state, .detail = detail, .result = result, .error = error});
}
void conversation_emit_emotion(const std::string &emotion) {
  emit({.kind = lunaredu::pet::conversation::AssistantEventKind::Emotion,
        .value = emotion.c_str()});
}
void conversation_emit_transcript(const std::string &text) {
  emit({.kind = lunaredu::pet::conversation::AssistantEventKind::Transcript,
        .value = text.c_str()});
}

std::string conversation_host_last_error() {
  const auto callbacks = host_callbacks_snapshot();
  return callbacks.last_error != nullptr
             ? callbacks.last_error(callbacks.context)
             : std::string();
}

bool conversation_request_audio_focus(const char *reason, uint32_t timeout_ms) {
  const auto callbacks = host_callbacks_snapshot();
  if (callbacks.request_audio_focus == nullptr) {
    return true;
  }
  return callbacks.request_audio_focus(callbacks.context, reason, timeout_ms);
}

void conversation_release_audio_focus(const char *reason) {
  const auto callbacks = host_callbacks_snapshot();
  if (callbacks.release_audio_focus != nullptr)
    callbacks.release_audio_focus(callbacks.context,
                                  reason == nullptr ? "session_end" : reason);
}
