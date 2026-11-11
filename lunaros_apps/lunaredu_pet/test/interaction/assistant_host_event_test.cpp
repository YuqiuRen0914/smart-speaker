#include <iostream>
#include <cassert>
#include "lunaredu_pet_interaction.h"
#include <string>

#include "lunaredu_pet_conversation.h"
#include "interaction/conversation/host_events.h"

static int core_event_count = 0;
namespace lunaredu::pet {
bool post_interaction_event(const InteractionEvent &) { ++core_event_count; return true; }
}
namespace {

struct Capture {
  int event_count = 0;
  lunaredu::pet::conversation::AssistantEventKind last_kind =
      lunaredu::pet::conversation::AssistantEventKind::Status;
  std::string value;
  std::string detail;
  std::string result;
  std::string error;
  std::string focus_reason;
  uint32_t focus_timeout_ms = 0;
  std::string released_focus_reason;
};

std::string copy(const char *value) {
  return value == nullptr ? std::string() : std::string(value);
}

void capture_event(void *context,
                   const lunaredu::pet::conversation::AssistantEvent &event) {
  auto &capture = *static_cast<Capture *>(context);
  capture.event_count += 1;
  capture.last_kind = event.kind;
  capture.value = copy(event.value);
  capture.detail = copy(event.detail);
  capture.result = copy(event.result);
  capture.error = copy(event.error);
}

std::string capture_last_error(void *) { return "pet_host_error"; }

bool capture_audio_focus(void *context, const char *reason,
                         uint32_t timeout_ms) {
  auto &capture = *static_cast<Capture *>(context);
  capture.focus_reason = copy(reason);
  capture.focus_timeout_ms = timeout_ms;
  return true;
}

void capture_audio_focus_release(void *context, const char *reason) {
  static_cast<Capture *>(context)->released_focus_reason = copy(reason);
}

} // namespace

int main() {
  Capture capture;
  lunaredu::pet::conversation::HostCallbacks callbacks;
  callbacks.context = &capture;
  callbacks.on_event = capture_event;
  callbacks.last_error = capture_last_error;
  callbacks.request_audio_focus = capture_audio_focus;
  callbacks.release_audio_focus = capture_audio_focus_release;
  conversation_set_host_callbacks(callbacks);

  conversation_emit_status("listening", "detail", "result", "error");
  if (capture.event_count != 1 ||
      capture.last_kind != lunaredu::pet::conversation::AssistantEventKind::Status ||
      capture.value != "listening" || capture.detail != "detail" ||
      capture.result != "result" || capture.error != "error") {
    return 1;
  }

  conversation_emit_emotion("happy");
  if (capture.event_count != 2 ||
      capture.last_kind != lunaredu::pet::conversation::AssistantEventKind::Emotion ||
      capture.value != "happy") {
    return 2;
  }

  conversation_emit_transcript("hello");
  if (capture.event_count != 3 ||
      capture.last_kind !=
          lunaredu::pet::conversation::AssistantEventKind::Transcript ||
      capture.value != "hello") {
    return 3;
  }

  if (conversation_host_last_error() != "pet_host_error" ||
      !conversation_request_audio_focus("manual_start", 500) ||
      capture.focus_reason != "manual_start" ||
      capture.focus_timeout_ms != 500) {
    return 4;
  }
  conversation_release_audio_focus("one_shot_response_finished");
  if (capture.released_focus_reason != "one_shot_response_finished") return 5;

  assert(core_event_count == 2); // Transcript is not a behavior event.
  conversation_set_host_callbacks({});
  conversation_emit_emotion("sad");
  assert(core_event_count == 3); // Works without product callbacks.
  assert(capture.event_count == 3);
  std::cout << "13 assistant host event checks passed" << std::endl;
  return 0;
}
