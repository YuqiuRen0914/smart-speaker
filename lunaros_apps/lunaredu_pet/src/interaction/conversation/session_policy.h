#pragma once

#include <cstdint>

namespace lunaredu::pet::conversation {

enum class ConversationResponseCompletion {
  ReturnToStandby,
  ResumeListening,
  EndOneShotTest,
};

inline constexpr uint8_t kConversationTestAudioRetryLimit = 1;

enum class ConversationTestAudioRecovery {
  ReopenChannel,
  ReopenChannelAndRetry,
};

struct StandbyCapturePolicyInput {
  bool service_available = false;
  bool network_connected = false;
  bool activation_ready = false;
  bool wake_frontend_ready = false;
  bool wake_word_mode = false;
  bool wake_word_enabled = false;
  bool wake_word_detection_in_listening = false;
  bool continuous_mode = false;
  bool session_idle = false;
  bool listening = false;
  bool speaking = false;
  bool session_start_inflight = false;
  bool awaiting_response = false;
  bool wake_word_resume_allowed = false;
};

struct WakeFrontendInitializationPolicyInput {
  bool service_available = false;
  bool initialized = false;
  bool initialization_in_progress = false;
  bool initialization_task_running = false;
  bool release_in_progress = false;
  bool release_task_running = false;
  bool retry_due = false;
};

constexpr bool wake_frontend_initialization_allowed(
    const WakeFrontendInitializationPolicyInput &input) {
  return input.service_available && input.retry_due && !input.initialized &&
         !input.initialization_in_progress &&
         !input.initialization_task_running && !input.release_in_progress &&
         !input.release_task_running;
}

constexpr bool wake_frontend_reconfiguration_required(
    bool wake_word_mode, bool frontend_initialized,
    bool requested_wake_word_available) {
  return wake_word_mode && frontend_initialized &&
         !requested_wake_word_available;
}

constexpr bool standby_wake_word_capture_allowed(
    const StandbyCapturePolicyInput &input) {
  return input.service_available && input.wake_frontend_ready &&
         input.wake_word_mode && input.wake_word_enabled &&
         !input.wake_word_detection_in_listening && !input.listening &&
         !input.speaking && !input.session_start_inflight &&
         !input.awaiting_response && input.wake_word_resume_allowed;
}

constexpr bool continuous_standby_capture_allowed(
    const StandbyCapturePolicyInput &input) {
  return input.service_available && input.network_connected &&
         input.activation_ready && input.wake_frontend_ready &&
         input.continuous_mode && input.session_idle && !input.listening &&
         !input.session_start_inflight && !input.awaiting_response &&
         input.wake_word_resume_allowed;
}

constexpr ConversationTestAudioRecovery conversation_test_audio_recovery(
    bool one_shot_test_active, bool test_text_available,
    uint8_t completed_audio_retries) {
  return one_shot_test_active && test_text_available &&
                 completed_audio_retries < kConversationTestAudioRetryLimit
             ? ConversationTestAudioRecovery::ReopenChannelAndRetry
             : ConversationTestAudioRecovery::ReopenChannel;
}

constexpr ConversationResponseCompletion conversation_response_completion(
    bool automatic_resume_enabled, bool one_shot_test_active,
    bool microphone_enabled = true) {
  if (one_shot_test_active) {
    return ConversationResponseCompletion::EndOneShotTest;
  }
  return automatic_resume_enabled && microphone_enabled
             ? ConversationResponseCompletion::ResumeListening
             : ConversationResponseCompletion::ReturnToStandby;
}

} // namespace lunaredu::pet::conversation
