#include <iostream>

#include "interaction/conversation/assistant_events.h"
#include "interaction/conversation/session_policy.h"

using lunaredu::pet::conversation::ConversationResponseCompletion;
using lunaredu::pet::conversation::ConversationTestAudioRecovery;
using lunaredu::pet::conversation::conversation_response_completion;
using lunaredu::pet::conversation::conversation_test_audio_recovery;
using lunaredu::pet::conversation::continuous_standby_capture_allowed;
using lunaredu::pet::conversation::standby_wake_word_capture_allowed;
using lunaredu::pet::conversation::wake_frontend_initialization_allowed;
using lunaredu::pet::conversation::wake_frontend_reconfiguration_required;

static_assert(conversation_response_completion(false, false) ==
              ConversationResponseCompletion::ReturnToStandby);
static_assert(conversation_response_completion(true, false) ==
              ConversationResponseCompletion::ResumeListening);
static_assert(conversation_response_completion(false, true) ==
              ConversationResponseCompletion::EndOneShotTest);
static_assert(conversation_response_completion(true, true) ==
              ConversationResponseCompletion::EndOneShotTest);
static_assert(conversation_response_completion(true, false, false) ==
              ConversationResponseCompletion::ReturnToStandby);
static_assert(conversation_response_completion(false, false, false) ==
              ConversationResponseCompletion::ReturnToStandby);

static_assert(conversation_test_audio_recovery(false, true, 0) ==
              ConversationTestAudioRecovery::ReopenChannel);
static_assert(conversation_test_audio_recovery(true, false, 0) ==
              ConversationTestAudioRecovery::ReopenChannel);
static_assert(conversation_test_audio_recovery(true, true, 0) ==
              ConversationTestAudioRecovery::ReopenChannelAndRetry);
static_assert(conversation_test_audio_recovery(true, true, 1) ==
              ConversationTestAudioRecovery::ReopenChannel);

constexpr lunaredu::pet::conversation::StandbyCapturePolicyInput kWakeWordOffline{
    .service_available = true,
    .network_connected = false,
    .activation_ready = false,
    .wake_frontend_ready = true,
    .wake_word_mode = true,
    .wake_word_enabled = true,
    .wake_word_detection_in_listening = false,
    .continuous_mode = false,
    .session_idle = false,
    .listening = false,
    .speaking = false,
    .session_start_inflight = false,
    .awaiting_response = false,
    .wake_word_resume_allowed = true,
};
static_assert(standby_wake_word_capture_allowed(kWakeWordOffline));

constexpr auto continuous_input() {
  auto input = kWakeWordOffline;
  input.wake_word_mode = false;
  input.wake_word_enabled = false;
  input.continuous_mode = true;
  input.session_idle = true;
  return input;
}
static_assert(!continuous_standby_capture_allowed(continuous_input()));
constexpr auto connected_continuous_input() {
  auto input = continuous_input();
  input.network_connected = true;
  input.activation_ready = true;
  return input;
}
static_assert(continuous_standby_capture_allowed(connected_continuous_input()));

constexpr lunaredu::pet::conversation::WakeFrontendInitializationPolicyInput
    kFrontendInitialization{
        .service_available = true,
        .initialized = false,
        .initialization_in_progress = false,
        .initialization_task_running = false,
        .release_in_progress = false,
        .release_task_running = false,
        .retry_due = true,
    };
static_assert(wake_frontend_initialization_allowed(kFrontendInitialization));
static_assert(wake_frontend_reconfiguration_required(true, true, false));
static_assert(!wake_frontend_reconfiguration_required(false, true, false));

static_assert(lunaredu::pet::conversation::internal::assistant_event_survives_suspension(
    lunaredu::pet::conversation::internal::AssistantEventType::WakeWordStartRequested));
static_assert(lunaredu::pet::conversation::internal::assistant_event_survives_suspension(
    lunaredu::pet::conversation::internal::AssistantEventType::ManualStartRequested));
static_assert(!lunaredu::pet::conversation::internal::assistant_event_survives_suspension(
    lunaredu::pet::conversation::internal::AssistantEventType::TransportDisconnected));

int main() {
  std::cout << "20 conversation policy checks passed" << std::endl;
  return 0;
}
