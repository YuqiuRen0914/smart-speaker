#include "interaction/voice/resident_audio.h"
#include "interaction/conversation/assistant_session_internal.h"
#include "interaction/conversation/assistant_core.h"
#include "interaction/conversation/assistant_loop.h"
#include "interaction/conversation/session_policy.h"
#include "lunaredu_pet_conversation_policy.h"
#include "lunaredu_audio_bridge.h"
#include "interaction/conversation/host_events.h"
#include "interaction/transport/assistant_transport.h"

#include <cinttypes>
#include <cstring>

namespace lunaredu::pet::conversation::internal {

void set_session_state_locked(AssistantSessionState state) {
  const AssistantSessionState previous = g_runtime.session_state;
  if (!assistant_session_transition(g_runtime.session_state, state)) {
    g_runtime.session_state = state;
  }
  if (previous != g_runtime.session_state) {
    ESP_LOGI(kTag,
             "assistant state transition %s -> %s mode=%s standby=%s session=%s",
             assistant_session_state_name(previous),
             assistant_session_state_name(g_runtime.session_state),
             g_runtime.conversation.listening_mode.c_str(),
             standby_listen_mode_name(g_runtime.conversation.standby_mode),
             g_runtime.transport.session_id.empty()
                 ? "-"
                 : g_runtime.transport.session_id.c_str());
  }
}

bool session_transport_available_locked() {
  return g_runtime.transport.connected;
}

bool session_active_locked() {
  return session_identity_available_locked() &&
         session_has_announcement_locked();
}

bool session_idle_available_locked() {
  return g_runtime.session_state == AssistantSessionState::ReadyIdle ||
         (g_runtime.session_state == AssistantSessionState::Inactive &&
          !session_active_locked() && session_transport_available_locked());
}

bool session_waiting_for_announcement_locked() {
  return session_connect_inflight_locked() &&
         session_transport_available_locked() &&
         !session_active_locked();
}

bool session_identity_available_locked() {
  return !g_runtime.transport.session_id.empty();
}

bool session_control_channel_present_locked() {
  return g_runtime.transport.mqtt_session != nullptr;
}

bool session_control_channel_connected_locked() {
  return g_runtime.transport.mqtt_session != nullptr &&
         g_runtime.transport.mqtt_session->IsConnected();
}

bool session_control_path_ready_locked() {
  return session_transport_available_locked() &&
         session_control_channel_present_locked() &&
         session_control_channel_connected_locked();
}

bool session_audio_path_ready_locked() {
  if (!session_control_path_ready_locked()) {
    return false;
  }
  return g_runtime.transport.udp_channel != nullptr &&
         g_runtime.transport.udp_channel->IsConnected();
}

bool session_listening_locked() {
  return g_runtime.session_state == AssistantSessionState::Listening;
}

bool session_start_inflight_locked() {
  return g_runtime.conversation.start_pending;
}

bool session_connect_inflight_locked() {
  return g_runtime.transport.connect_pending;
}

bool session_has_announcement_locked() {
  return g_runtime.transport.session_announced;
}

bool session_can_start_listening_locked() {
  if (session_start_inflight_locked() || session_connect_inflight_locked()) {
    return false;
  }
  if (g_runtime.session_state == AssistantSessionState::Opening ||
      g_runtime.session_state == AssistantSessionState::Closing ||
      g_runtime.session_state == AssistantSessionState::Speaking ||
      g_runtime.session_state == AssistantSessionState::Listening) {
    return false;
  }
  return session_transport_available_locked();
}

bool session_can_finish_listening_locked() {
  return session_control_path_ready_locked() &&
         session_listening_locked() &&
         g_runtime.conversation.listening_mode != "realtime";
}

bool session_can_stop_listening_locked() {
  return session_control_path_ready_locked() && session_listening_locked();
}

bool session_can_abort_speaking_locked() {
  return session_control_path_ready_locked() &&
         g_runtime.session_state == AssistantSessionState::Speaking &&
         session_identity_available_locked();
}

void set_session_start_inflight_locked(bool inflight) {
  g_runtime.conversation.start_pending = inflight;
}

void set_session_connect_inflight_locked(bool inflight) {
  g_runtime.transport.connect_pending = inflight;
}

void set_session_has_announcement_locked(bool announced) {
  g_runtime.transport.session_announced = announced;
}

bool session_should_send_listen_start_locked() {
  return lunaredu::pet::conversation::assistant_microphone_enabled() &&
         session_start_inflight_locked() && session_ready_locked();
}

void session_reset_supporting_facts_locked() {
  set_session_has_announcement_locked(false);
  set_session_connect_inflight_locked(false);
  set_session_start_inflight_locked(false);
}

bool session_ready_locked() {
  if (!session_transport_available_locked() || !session_active_locked()) {
    return false;
  }
  return session_audio_path_ready_locked();
}

void maybe_post_audio_playback_finished_locked(const char *source) {
  if (!g_runtime.conversation.tts_stop_waiting_for_playback) {
    return;
  }

  const int64_t now = now_ms();
  if (g_runtime.conversation.tts_stop_playback_grace_deadline_ms > 0 &&
      now < g_runtime.conversation.tts_stop_playback_grace_deadline_ms) {
    ESP_LOGI(kTag,
             "audio playback wait source=%s grace_remaining_ms=%lld mode=%s session=%s state=%s",
             source == nullptr ? "-" : source,
             static_cast<long long>(
                 g_runtime.conversation.tts_stop_playback_grace_deadline_ms -
                 now),
             g_runtime.conversation.listening_mode.c_str(),
             g_runtime.transport.session_id.empty()
                 ? "-"
                 : g_runtime.transport.session_id.c_str(),
             assistant_session_state_name(g_runtime.session_state));
    return;
  }

  const UBaseType_t queued =
      g_runtime.downlink_audio_queue == nullptr
          ? 0
          : uxQueueMessagesWaiting(g_runtime.downlink_audio_queue);
  if (g_runtime.conversation.downlink_audio_active || queued > 0) {
    ESP_LOGI(kTag,
             "audio playback wait source=%s active=%d queued=%u mode=%s session=%s state=%s",
             source == nullptr ? "-" : source,
             g_runtime.conversation.downlink_audio_active,
             static_cast<unsigned>(queued),
             g_runtime.conversation.listening_mode.c_str(),
             g_runtime.transport.session_id.empty()
                 ? "-"
                 : g_runtime.transport.session_id.c_str(),
             assistant_session_state_name(g_runtime.session_state));
    return;
  }

  g_runtime.conversation.tts_stop_waiting_for_playback = false;
  g_runtime.conversation.tts_stop_playback_grace_deadline_ms = 0;
  const auto audio_status =
      lunaredu::audio::local_pcm_audio_bridge_status();
  const bool received_audio =
      g_runtime.incoming_audio_packets >
      g_runtime.speaking_incoming_audio_packets_base;
  auto &wire = g_runtime.transport.wire;
  if (kAssistantUsesLunarLink && wire.terminal_seen && !wire.receipt_sent && !wire.activity_id.empty()) {
    const bool delivered = wire.playback_delivered(received_audio,
        !g_runtime.diagnostics.last_playback_error.empty(),
        !g_runtime.diagnostics.last_binary_error.empty());
    if (!g_runtime.pending_mcp_messages.try_push_front(wire.playback_receipt(
            g_runtime.transport.session_id, delivered))) {
      g_runtime.diagnostics.last_error = "lunarlink_playback_receipt_queue_full";
      g_runtime.conversation.tts_stop_waiting_for_playback = true;
      return;
    }
    wire.receipt_sent = true;
  }
  if (!received_audio) {
    auto &conversation = g_runtime.conversation;
    const auto recovery =
        lunaredu::pet::conversation::conversation_test_audio_recovery(
            conversation.one_shot_test_active,
            !conversation.one_shot_test_text.empty(),
            conversation.one_shot_test_audio_retry_count);
    const bool retry_test =
        recovery == lunaredu::pet::conversation::
                        ConversationTestAudioRecovery::ReopenChannelAndRetry;
    if (retry_test) {
      conversation.one_shot_test_audio_retry_count += 1;
      conversation.pending_test_message = conversation.one_shot_test_text;
      conversation.pending_test_is_audio_retry = true;
      conversation.one_shot_test_active = false;
      conversation.awaiting_response = false;
    } else if (conversation.one_shot_test_active) {
      conversation.one_shot_test_active = false;
      conversation.pending_test_message.clear();
      conversation.pending_test_is_audio_retry = false;
      conversation.one_shot_test_text.clear();
      conversation.one_shot_test_audio_retry_count = 0;
    }
    g_runtime.diagnostics.last_playback_error = "tts_audio_not_received";
    g_runtime.diagnostics.last_error =
        g_runtime.diagnostics.last_playback_error;
    g_runtime.close_audio_channel_requested.store(true);
    ESP_LOGW(kTag,
             "tts playback finished without UDP audio; rebuilding channel "
             "incoming_audio=%" PRIu32 " baseline=%" PRIu32
             " retry_test=%d",
             g_runtime.incoming_audio_packets,
             g_runtime.speaking_incoming_audio_packets_base, retry_test);
    (void)post_assistant_event_locked(
        {AssistantEventType::ReopenMqttAudioChannel,
         retry_test ? "正在重建音频通道并重试" : "正在重建音频通道",
         retry_test ? "字幕已到达，正在重试语音"
                    : "字幕已到达但未收到音频",
         true});
    conversation_emit_status(
        retry_test ? "语音重试" : "连接异常", "收到字幕但未收到音频",
        retry_test ? "重建通道后自动重试一次" : "正在重建音频通道",
        nullptr);
    return;
  }
  ESP_LOGI(kTag,
           "audio playback finished source=%s output_frames=%u incoming_audio=%u resume_candidate=%d mode=%s session=%s state=%s",
           source == nullptr ? "-" : source,
           static_cast<unsigned>(audio_status.output_frames),
           static_cast<unsigned>(g_runtime.incoming_audio_packets),
           conversation_auto_resume_enabled_locked(),
           g_runtime.conversation.listening_mode.c_str(),
           g_runtime.transport.session_id.empty()
               ? "-"
               : g_runtime.transport.session_id.c_str(),
           assistant_session_state_name(g_runtime.session_state));
  (void)post_assistant_event_locked(
      {AssistantEventType::AudioPlaybackFinished, {}, {}, false});
}


namespace {

const char *idle_ready_state_label_locked() {
  return g_runtime.conversation.standby_mode ==
                 StandbyListenMode::ContinuousConversation
             ? "连续对话"
             : "待唤醒";
}

const char *idle_ready_result_text_locked() {
  return g_runtime.conversation.standby_mode ==
                 StandbyListenMode::ContinuousConversation
             ? "等待自动开始持续监听"
             : "请说唤醒词";
}

} // namespace

void enter_connecting_state_locked(const char *detail, const char *result,
                                   bool clear_session_identity) {
  if (clear_session_identity) {
    set_session_has_announcement_locked(false);
    g_runtime.transport.session_id.clear();
  }
  set_session_state_locked(AssistantSessionState::Opening);
  set_session_connect_inflight_locked(true);
  g_runtime.transport.connect_started_at_ms = now_ms();
  g_runtime.conversation.idle_entered_at_ms = 0;
  g_runtime.diagnostics.last_error.clear();
  g_runtime.diagnostics.last_transport_error.clear();
  conversation_emit_status("连接中",
                            detail == nullptr ? "正在连接助手后端" : detail,
                            result == nullptr ? "自动连接" : result, nullptr);
}

void enter_idle_ready_state_locked(const char *detail, const char *result) {
  set_session_state_locked(AssistantSessionState::ReadyIdle);
  set_session_connect_inflight_locked(false);
  g_runtime.transport.connect_started_at_ms = 0;
  g_runtime.transport.last_keepalive_sent_at_ms = 0;
  g_runtime.conversation.idle_entered_at_ms = now_ms();
  g_runtime.conversation.listening_started_at_ms = 0;
  g_runtime.conversation.speaking_started_at_ms = 0;
  g_runtime.conversation.session_close_deadline_ms = 0;
  g_runtime.conversation.awaiting_response = false;
  g_runtime.conversation.tts_stop_waiting_for_playback = false;
  g_runtime.conversation.tts_stop_playback_grace_deadline_ms = 0;
  g_runtime.diagnostics.last_error.clear();
  g_runtime.vad.speech_ms = 0;
  g_runtime.vad.silence_ms = 0;
  g_runtime.vad.voice_activity_active = false;
  g_runtime.vad.utterance_started = false;
  conversation_emit_status(idle_ready_state_label_locked(),
                            detail == nullptr ? "已连接助手" : detail,
                            result == nullptr ? idle_ready_result_text_locked()
                                              : result,
                            nullptr);
}

void enter_speaking_state_locked(bool entering_speaking,
                                 const char *detail,
                                 const char *result) {
  set_session_state_locked(AssistantSessionState::Speaking);
  g_runtime.conversation.listening_started_at_ms = 0;
  g_runtime.conversation.awaiting_response = false;
  if (entering_speaking) {
    g_runtime.conversation.speaking_started_at_ms = now_ms();
    g_runtime.speaking_incoming_audio_packets_base =
        g_runtime.incoming_audio_packets;
  }
  g_runtime.conversation.session_close_deadline_ms = 0;
  if (entering_speaking) {
    conversation_emit_status("播报中",
                              detail == nullptr ? "助手正在回复" : detail,
                              result == nullptr
                                  ? g_runtime.diagnostics.last_message_summary.c_str()
                                  : result,
                              nullptr);
  }
}

void enter_realtime_listening_state_locked(const char *detail,
                                           const char *result) {
  set_session_state_locked(AssistantSessionState::Listening);
  g_runtime.conversation.listening_started_at_ms = now_ms();
  g_runtime.listening_sent_audio_packets_base = g_runtime.sent_audio_packets;
  g_runtime.conversation.speaking_started_at_ms = 0;
  g_runtime.conversation.session_close_deadline_ms = 0;
  g_runtime.conversation.awaiting_response = false;
  g_runtime.conversation.tts_stop_waiting_for_playback = false;
  g_runtime.conversation.tts_stop_playback_grace_deadline_ms = 0;
  conversation_emit_status("正在听",
                            detail == nullptr ? "实时会话中" : detail,
                            result == nullptr
                                ? g_runtime.diagnostics.last_message_summary.c_str()
                                : result,
                            nullptr);
}

void enter_idle_after_tts_stop_locked(const char *detail, const char *result) {
  set_session_state_locked(AssistantSessionState::Closing);
  g_runtime.conversation.idle_entered_at_ms = now_ms();
  g_runtime.conversation.listening_started_at_ms = 0;
  g_runtime.conversation.speaking_started_at_ms = 0;
  g_runtime.conversation.awaiting_response = false;
  g_runtime.conversation.tts_stop_waiting_for_playback = false;
  g_runtime.conversation.tts_stop_playback_grace_deadline_ms = 0;
  g_runtime.vad.speech_ms = 0;
  g_runtime.vad.silence_ms = 0;
  g_runtime.vad.voice_activity_active = false;
  g_runtime.vad.utterance_started = false;
  // In continuous conversation mode, use minimal delay
  if (g_runtime.conversation.standby_mode == StandbyListenMode::ContinuousConversation) {
    g_runtime.conversation.wake_word_resume_not_before_ms = 0;
    g_runtime.conversation.session_close_deadline_ms = 0;
  } else {
    g_runtime.conversation.wake_word_resume_not_before_ms =
        g_runtime.conversation.idle_entered_at_ms +
        static_cast<int64_t>(kSessionCloseGraceMs);
    g_runtime.conversation.session_close_deadline_ms =
        g_runtime.conversation.idle_entered_at_ms +
        static_cast<int64_t>(kSessionCloseGraceMs);
  }
  lunaredu::pet::conversation::audio_io::enable_output(false);
  lunaredu::pet::conversation::audio_io::clear_output_reference();
  set_session_state_locked(AssistantSessionState::ReadyIdle);
  g_runtime.vad.speech_ms = 0;
  g_runtime.vad.silence_ms = 0;
  g_runtime.vad.voice_activity_active = false;
  g_runtime.vad.utterance_started = false;
  conversation_emit_status(idle_ready_state_label_locked(),
                            detail == nullptr ? "已播报完成，等待会话收尾"
                                              : detail,
                            result == nullptr
                                ? (g_runtime.conversation.standby_mode ==
                                           StandbyListenMode::ContinuousConversation
                                       ? "准备开始下一轮连续短句"
                                       : "等待服务端结束本轮会话")
                                : result,
                            nullptr);
}

void mark_transport_connected_waiting_session_locked() {
  g_runtime.transport.connected = true;
  set_session_has_announcement_locked(false);
  set_session_connect_inflight_locked(false);
  g_runtime.transport.connect_started_at_ms = 0;
  g_runtime.conversation.idle_entered_at_ms = now_ms();
  g_runtime.diagnostics.last_transport_error.clear();
  if (g_runtime.session_state == AssistantSessionState::Inactive ||
      g_runtime.session_state == AssistantSessionState::Failed) {
    set_session_state_locked(AssistantSessionState::Opening);
  }
}

void enter_idle_after_stop_locked(const char *detail, const char *result,
                                   AssistantSessionState next_state) {
  set_session_state_locked(AssistantSessionState::Closing);
  g_runtime.conversation.idle_entered_at_ms = now_ms();
  // In continuous conversation mode, don't add delay for wake word resume
  if (g_runtime.conversation.standby_mode == StandbyListenMode::ContinuousConversation) {
    g_runtime.conversation.wake_word_resume_not_before_ms = 0;
  } else {
    g_runtime.conversation.wake_word_resume_not_before_ms =
        g_runtime.conversation.idle_entered_at_ms +
        static_cast<int64_t>(kWakeWordResumeDelayMs);
  }
  set_session_start_inflight_locked(false);
  g_runtime.conversation.session_close_deadline_ms = 0;
  g_runtime.conversation.local_audio_capture_not_before_ms = 0;
  g_runtime.transport.audio_uplink_timestamp_ms = 0;
  g_runtime.vad.voice_activity_active = false;
  g_runtime.vad.speech_ms = 0;
  g_runtime.vad.silence_ms = 0;
  g_runtime.vad.current_utterance_ms = 0;
  g_runtime.vad.utterance_started = false;
  g_runtime.diagnostics.last_error.clear();
  g_runtime.diagnostics.last_binary_error.clear();
  g_runtime.diagnostics.last_playback_error.clear();
  lunaredu::pet::conversation::audio_io::enable_input(false);
  lunaredu::pet::conversation::audio_io::enable_output(false);
  const bool restart_wake_word =
      lunaredu::pet::conversation::assistant_microphone_enabled() &&
      !(conversation_auto_resume_enabled_locked() &&
        g_runtime.conversation.awaiting_response);
  {
    std::lock_guard<std::mutex> ww_lock(g_runtime.wake_word_mutex);
    if (restart_wake_word) {
      g_runtime.wake_word_frontend.start();
    } else {
      g_runtime.wake_word_frontend.stop();
    }
  }
  ESP_LOGI(kTag,
           "listen stop committed reason=%s next=%s restart_wakenet=%d awaiting_response=%d mode=%s session=%s",
           g_runtime.conversation.last_stop_result.empty()
               ? "-"
               : g_runtime.conversation.last_stop_result.c_str(),
           assistant_session_state_name(next_state), restart_wake_word,
           g_runtime.conversation.awaiting_response,
           g_runtime.conversation.listening_mode.c_str(),
           g_runtime.transport.session_id.empty()
               ? "-"
               : g_runtime.transport.session_id.c_str());
  set_session_state_locked(next_state);
  persist_state_locked();
  conversation_emit_status(idle_ready_state_label_locked(),
                            detail == nullptr ? "已结束收音" : detail,
                            result == nullptr ? idle_ready_result_text_locked()
                                              : result,
                            nullptr);
}

void enter_idle_after_connect_failure_locked(const char *error,
                                             const char *transport_error,
                                             bool clear_session_identity) {
  set_session_state_locked(AssistantSessionState::Failed);
  set_session_connect_inflight_locked(false);
  g_runtime.transport.connected = false;
  g_runtime.transport.connect_started_at_ms = 0;
  g_runtime.conversation.idle_entered_at_ms = now_ms();
  if (clear_session_identity) {
    set_session_has_announcement_locked(false);
    g_runtime.transport.session_id.clear();
  }
  g_runtime.diagnostics.last_error = error == nullptr ? "" : error;
  g_runtime.diagnostics.last_transport_error =
      transport_error == nullptr ? "" : transport_error;
}

void enter_listening_state_locked() {
  const AssistantSessionState previous = g_runtime.session_state;
  set_session_state_locked(AssistantSessionState::Listening);
  g_runtime.conversation.idle_entered_at_ms = 0;
  g_runtime.conversation.listening_started_at_ms = now_ms();
  g_runtime.conversation.speaking_started_at_ms = 0;
  g_runtime.conversation.awaiting_response = false;
  g_runtime.conversation.tts_stop_waiting_for_playback = false;
  g_runtime.conversation.tts_stop_playback_grace_deadline_ms = 0;
  g_runtime.listening_sent_audio_packets_base = g_runtime.sent_audio_packets;
  g_runtime.conversation.last_stop_result.clear();
  g_runtime.conversation.local_audio_capture_not_before_ms =
      now_ms() + kLocalAudioCaptureStartDelayMs;
  g_runtime.vad.voice_activity_active = false;
  g_runtime.vad.pcm_frames = 0;
  g_runtime.vad.speech_frames = 0;
  g_runtime.vad.silence_frames = 0;
  g_runtime.vad.last_rms = 0;
  g_runtime.vad.last_peak = 0;
  g_runtime.vad.current_utterance_ms = 0;
  g_runtime.vad.speech_ms = 0;
  g_runtime.vad.silence_ms = 0;
  g_runtime.vad.utterance_started = false;
  g_runtime.diagnostics.last_error.clear();
  persist_state_locked();
  conversation_emit_status("正在听", "请开始说话", "板载语音已开启",
                            nullptr);
  {
    std::lock_guard<std::mutex> ww_lock(g_runtime.wake_word_mutex);
    g_runtime.wake_word_frontend.stop();
  }
  ESP_LOGI(kTag,
           "listening opened state=%s -> %s mode=%s standby=%s session=%s udp=%d",
           assistant_session_state_name(previous),
           assistant_session_state_name(g_runtime.session_state),
           g_runtime.conversation.listening_mode.c_str(),
           standby_listen_mode_name(g_runtime.conversation.standby_mode),
           g_runtime.transport.session_id.empty()
               ? "-"
               : g_runtime.transport.session_id.c_str(),
           g_runtime.transport.udp_channel != nullptr);
}

PrepareStartListeningResult prepare_start_listening_locked(std::string &message) {
  if (!session_identity_available_locked()) {
    if (session_control_channel_connected_locked()) {
      return PrepareStartListeningResult::ReopenMqttAudioChannel;
    }
    set_session_start_inflight_locked(false);
    g_runtime.diagnostics.last_error = "server_hello_missing_session_id";
    return PrepareStartListeningResult::MissingSessionIdentity;
  }

  message = xiaozhi_build_listen_start_message(
      g_runtime.transport.session_id, g_runtime.conversation.listening_mode);
  return PrepareStartListeningResult::ReadyToSend;
}

void commit_start_listening_success_locked() {
  if (!lunaredu::pet::conversation::assistant_microphone_enabled()) {
    commit_start_listening_failure_locked();
    return;
  }
  enter_listening_state_locked();
  g_runtime.transport.audio_uplink_timestamp_ms = 0;
  set_session_start_inflight_locked(false);
  ESP_LOGI(kTag,
           "listen start sent ok mode=%s session=%s state=%s",
           g_runtime.conversation.listening_mode.c_str(),
           g_runtime.transport.session_id.empty()
               ? "-"
               : g_runtime.transport.session_id.c_str(),
           assistant_session_state_name(g_runtime.session_state));
}

void commit_start_listening_failure_locked() {
  set_session_start_inflight_locked(false);
  g_runtime.conversation.local_audio_capture_not_before_ms = 0;
}

bool stop_listening_now_locked(const char *reason, bool count_stop_request,
                               const char *idle_detail) {
  std::string message;
  bool send_required = false;
  const bool immediate_manual_stop =
      reason != nullptr && (std::strcmp(reason, "manual_finish") == 0 ||
                            std::strcmp(reason, "manual_stop") == 0);
  {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    if (!prepare_stop_listening_locked(reason, count_stop_request, idle_detail,
                                       message, send_required)) {
      return false;
    }
  }
  if (!send_required) {
    return true;
  }

  // Manual push-to-talk must acknowledge the user's release immediately.
  // Publishing the stop frame can briefly wait on MQTT; leaving the session
  // in Listening until that publish returns makes the display look stuck and
  // keeps the capture gate open during the network delay. The audio worker
  // checks the session state before accepting each encoded work item, so a
  // read already in progress may finish but no new audio is admitted.
  if (immediate_manual_stop) {
    {
      std::lock_guard<std::mutex> lock(g_runtime.mutex);
      commit_stop_listening_success_locked(reason, idle_detail, true);
    }

    const bool ok = send_service_text(message);
    if (!ok) {
      std::lock_guard<std::mutex> lock(g_runtime.mutex);
      commit_stop_listening_failure_locked();
    }
    return ok;
  }

  const bool ok = send_service_text(message);
  std::lock_guard<std::mutex> lock(g_runtime.mutex);
  if (!ok) {
    commit_stop_listening_failure_locked();
    return false;
  }

  commit_stop_listening_success_locked(reason, idle_detail, true);
  return true;
}

void stop_listening_for_microphone_mute() {
  std::string message;
  bool send_required = false;
  {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    set_session_start_inflight_locked(false);
    g_runtime.conversation.prime_uplink_requested = false;
    g_runtime.conversation.local_audio_capture_not_before_ms = 0;
    g_runtime.conversation.awaiting_response = false;
    if (!session_listening_locked()) {
      return;
    }
    if (!prepare_stop_listening_locked(
            "microphone_muted", false, "麦克风已关闭", message,
            send_required)) {
      return;
    }
  }

  const bool sent = !send_required || send_service_text(message);
  std::lock_guard<std::mutex> lock(g_runtime.mutex);
  if (!session_listening_locked()) {
    return;
  }
  if (!sent) {
    ESP_LOGW(kTag,
             "listen stop send failed during microphone mute; closing locally");
  }
  g_runtime.conversation.last_stop_result = "microphone_muted";
  g_runtime.conversation.awaiting_response = false;
  enter_idle_after_stop_locked("麦克风已关闭", "不会继续聆听",
                               AssistantSessionState::ReadyIdle);
}

bool prepare_stop_listening_locked(const char *reason, bool count_stop_request,
                                   const char *idle_detail,
                                   std::string &message, bool &send_required) {
  (void)count_stop_request;
  if (!session_listening_locked()) {
    g_runtime.conversation.last_stop_result =
        reason == nullptr ? "not_listening" : reason;
    ESP_LOGI(kTag,
             "listen stop ignored reason=%s state=%s mode=%s session=%s",
             g_runtime.conversation.last_stop_result.c_str(),
             assistant_session_state_name(g_runtime.session_state),
             g_runtime.conversation.listening_mode.c_str(),
             g_runtime.transport.session_id.empty()
                 ? "-"
                 : g_runtime.transport.session_id.c_str());
    send_required = false;
    return false;
  }
  if (!session_control_channel_present_locked() ||
      !session_identity_available_locked()) {
    g_runtime.conversation.last_stop_result = "noop";
    g_runtime.conversation.awaiting_response = false;
    enter_idle_after_stop_locked(
        idle_detail == nullptr ? "已结束收音" : idle_detail,
        g_runtime.conversation.standby_mode ==
                StandbyListenMode::ContinuousConversation
            ? "等待自动开始下一轮连续短句"
            : "请说唤醒词",
        AssistantSessionState::Inactive);
    send_required = false;
    return true;
  }

  message = xiaozhi_build_listen_stop_message(g_runtime.transport.session_id);
  send_required = true;
  return true;
}

void commit_stop_listening_success_locked(const char *reason,
                                          const char *idle_detail,
                                          bool send_required) {
  (void)send_required;
  g_runtime.conversation.last_stop_result = reason == nullptr ? "sent" : reason;
  g_runtime.conversation.awaiting_response =
      (g_runtime.conversation.last_stop_result == "auto_stop" &&
       conversation_auto_resume_enabled_locked()) ||
      g_runtime.conversation.last_stop_result == "manual_finish";
  enter_idle_after_stop_locked(
      idle_detail == nullptr ? "已结束收音" : idle_detail,
      g_runtime.conversation.awaiting_response
          ? "等待助手回复"
          : (g_runtime.conversation.standby_mode ==
                     StandbyListenMode::ContinuousConversation
                 ? "等待自动开始下一轮连续短句"
                 : "请说唤醒词"),
      AssistantSessionState::ReadyIdle);
  if (g_runtime.conversation.awaiting_response) {
    g_runtime.conversation.session_close_deadline_ms =
        now_ms() + static_cast<int64_t>(kAssistantResponseTimeoutMs);
  }
}

void commit_stop_listening_failure_locked() {
  g_runtime.conversation.last_stop_result = "send_failed";
}

} // namespace lunaredu::pet::conversation::internal
