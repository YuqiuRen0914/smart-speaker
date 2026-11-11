#include "interaction/voice/resident_audio.h"
#include "interaction/conversation/assistant_loop.h"
#include "interaction/conversation/assistant_turn_policy.h"

#include <cinttypes>
#include <utility>

#include <ArduinoJson.h>

#include "interaction/conversation/assistant_core.h"
#include "interaction/voice/assistant_audio.h"
#include "interaction/conversation/assistant_lifecycle.h"
#include "interaction/conversation/assistant_session.h"
#include "interaction/conversation/assistant_session_internal.h"
#include "lunaredu_audio_bridge.h"
#include "interaction/conversation/session_policy.h"
#include "lunaredu_pet_conversation_policy.h"
#include "interaction/transport/assistant_transport.h"
#include "interaction/conversation/host_events.h"

namespace lunaredu::pet::conversation::internal {

namespace {

constexpr size_t kAssistantEventQueueCapacity = 32;

bool event_requires_microphone(AssistantEventType type) {
  return type == AssistantEventType::ContinuousStartRequested ||
         type == AssistantEventType::WakeWordStartRequested ||
         type == AssistantEventType::StartListeningRequested ||
         type == AssistantEventType::ManualStartRequested;
}

bool wake_frontend_needs_wake_word_reinit_locked() {
  std::lock_guard<std::mutex> wake_lock(g_runtime.wake_word_mutex);
  const auto status = g_runtime.wake_word_frontend.status();
  return lunaredu::pet::conversation::wake_frontend_reconfiguration_required(
      true, status.initialized, status.wake_word_enabled);
}

void request_wake_frontend_mode_reinit(bool required) {
  if (required && !request_wake_word_frontend_release()) {
    ESP_LOGE(kTag, "failed to queue wake frontend mode reinitialization");
  }
}

bool apply_connect_service_action_locked(ConnectServiceAction action,
                                         const char *connect_error) {
  switch (action) {
  case ConnectServiceAction::AlreadyInflight:
    return true;
  case ConnectServiceAction::ReadyIdle:
    g_runtime.transport.connected = true;
    set_session_has_announcement_locked(true);
    g_runtime.diagnostics.last_error.clear();
    enter_idle_ready_state_locked("已连接助手", nullptr);
    return true;
  case ConnectServiceAction::WaitAnnouncement:
    g_runtime.transport.connected = true;
    enter_connecting_state_locked("等待助手会话握手", "自动连接", false);
    return true;
  case ConnectServiceAction::ReopenMqttAudioChannel:
    g_runtime.transport.connected = true;
    (void)post_assistant_event_locked(
        {AssistantEventType::ReopenMqttAudioChannel, "正在重新打开音频通道",
         "自动连接", true});
    return true;
  case ConnectServiceAction::StartConnecting:
    enter_connecting_state_locked("正在连接助手后端", "自动连接", false);
    return true;
  case ConnectServiceAction::MqttActivationIncomplete:
    g_runtime.diagnostics.last_error =
        "voice_assistant_mqtt_activation_incomplete";
    return false;
  case ConnectServiceAction::ActivationIncomplete:
    g_runtime.diagnostics.last_error = "voice_assistant_activation_incomplete";
    return false;
  default:
    return false;
  }
}

bool request_continuous_listening_start_locked() {
  if (!lunaredu::pet::conversation::assistant_microphone_enabled()) {
    return false;
  }
  if (g_runtime.conversation.standby_mode !=
      StandbyListenMode::ContinuousConversation) {
    return false;
  }
  if (!session_idle_available_locked() ||
      session_listening_locked() || session_start_inflight_locked() ||
      session_connect_inflight_locked() || !assistant_network_connected() ||
      !g_runtime.activation.ready) {
    return false;
  }
  g_runtime.conversation.listening_mode = "auto";
  set_session_start_inflight_locked(true);
  const ConnectServiceAction action = derive_connect_service_action_locked();
  if (!apply_connect_service_action_locked(action,
                                           "continuous_start_connect_failed")) {
    set_session_start_inflight_locked(false);
    return false;
  }
  conversation_emit_status("连续对话", "正在自动开始连续短句对话",
                            "按 BOOT 可切回唤醒词模式", nullptr);
  return true;
}

void enqueue_periodic_events_locked(int64_t now) {
  const bool should_auto_connect =
      assistant_network_connected() && g_runtime.activation.ready &&
      !session_connect_inflight_locked() && !g_runtime.transport.connected;
  if (should_auto_connect) {
    (void)post_assistant_event_locked(
        {AssistantEventType::ConnectRequested, {}, {}, false});
  }

  // In continuous mode, listening is now started by VAD in poll_local_audio_once

  const bool should_force_close_stale_session =
      session_idle_available_locked() &&
      g_runtime.session_state != AssistantSessionState::Listening &&
      g_runtime.conversation.session_close_deadline_ms > 0 &&
      now >= g_runtime.conversation.session_close_deadline_ms &&
      session_ready_locked();
  if (should_force_close_stale_session) {
    (void)post_assistant_event_locked(
        {AssistantEventType::StaleSessionCloseDue, {}, {}, false});
  }

  if (g_runtime.conversation.tts_stop_waiting_for_playback) {
    maybe_post_audio_playback_finished_locked("periodic_playback_drain");
  }

  const bool should_end_single_turn_without_speech =
      should_timeout_empty_single_turn(
          g_runtime.conversation.listening_mode.c_str(),
          g_runtime.session_state == AssistantSessionState::Listening,
          g_runtime.conversation.listening_started_at_ms,
          g_runtime.vad.utterance_started, now,
          kAutoConversationNoSpeechTimeoutMs);
  if (should_end_single_turn_without_speech) {
    (void)post_assistant_event_locked(
        {AssistantEventType::ListeningNoSpeechTimeoutDue, {}, {}, false});
  }

  const bool should_fall_back_to_wake_word =
      g_runtime.conversation.standby_mode ==
          StandbyListenMode::ContinuousConversation &&
      session_idle_available_locked() &&
      !session_start_inflight_locked() &&
      g_runtime.conversation.idle_entered_at_ms > 0 &&
      (now - g_runtime.conversation.idle_entered_at_ms) >=
          static_cast<int64_t>(kContinuousConversationIdleTimeoutMs);
  if (should_fall_back_to_wake_word) {
    (void)post_assistant_event_locked(
        {AssistantEventType::ContinuousIdleTimeoutDue, {}, {}, false});
  }

  if (session_connect_inflight_locked() &&
      g_runtime.transport.connect_started_at_ms > 0 &&
      (now - g_runtime.transport.connect_started_at_ms) >=
          static_cast<int64_t>(kConnectTimeoutMs)) {
    (void)post_assistant_event_locked(
        {AssistantEventType::ConnectionWatchdogDue, {}, {}, false});
  }

  if (session_should_send_listen_start_locked()) {
    (void)post_assistant_event_locked(
        {AssistantEventType::StartListeningRequested, {}, {}, false});
  }

  (void)post_assistant_event_locked(
      {AssistantEventType::FlushPendingMcp, {}, {}, false});
}

void handle_connect_requested(const AssistantEvent &event) {
  std::string queued_test_message;
  {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    if (event.detail == "queued_test_message" && !event.text.empty()) {
      // Keep the request on the event as well as in ConversationState. A stale
      // session-goodbye may legitimately clear the latter while this request is
      // waiting in the assistant queue.
      g_runtime.conversation.pending_test_message = event.text;
      g_runtime.conversation.pending_test_is_audio_retry = false;
      g_runtime.conversation.one_shot_test_text = event.text;
      g_runtime.conversation.one_shot_test_audio_retry_count = 0;
    }

    const ConnectServiceAction action = derive_connect_service_action_locked();
    if (!apply_connect_service_action_locked(action, "auto_connect_failed")) {
      g_runtime.diagnostics.last_error = "auto_connect_failed";
    } else {
      conversation_emit_status("连接中", "正在自动连接后端", "开机自动连接",
                            nullptr);
      // ReadyIdle does not produce another hello event. Consume a queued test
      // immediately when the existing session is already usable.
      if (action == ConnectServiceAction::ReadyIdle &&
          session_ready_locked() &&
          !g_runtime.conversation.pending_test_message.empty()) {
        queued_test_message =
            g_runtime.conversation.pending_test_message;
      }
    }
  }

  if (!queued_test_message.empty() &&
      !lunaredu::pet::conversation::send_test_message(queued_test_message.c_str())) {
    ESP_LOGW(kTag, "queued XiaoZhi test message could not be sent from ready session");
  }
}

void handle_continuous_start_requested() {
  std::lock_guard<std::mutex> lock(g_runtime.mutex);
  (void)request_continuous_listening_start_locked();
}

void handle_stale_session_close_due() {
  std::lock_guard<std::mutex> lock(g_runtime.mutex);
  if (session_idle_available_locked() &&
      g_runtime.session_state != AssistantSessionState::Listening &&
      g_runtime.conversation.session_close_deadline_ms > 0 &&
      now_ms() >= g_runtime.conversation.session_close_deadline_ms &&
      session_ready_locked()) {
    const bool response_timed_out =
        g_runtime.conversation.awaiting_response;
    close_audio_channel_locked(response_timed_out
                                   ? "助手回答超时，请重新唤醒"
                                   : "会话收尾超时，已本地关闭",
                               AssistantSessionState::ReadyIdle);
  }
}

void handle_connection_watchdog_due() {
  {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    if (!session_connect_inflight_locked() ||
        g_runtime.transport.connect_started_at_ms <= 0) {
      return;
    }

    const int64_t elapsed = now_ms() - g_runtime.transport.connect_started_at_ms;
    if (elapsed < static_cast<int64_t>(kConnectTimeoutMs)) {
      return;
    }

    if (session_control_channel_connected_locked() && !session_active_locked() &&
        g_runtime.transport.udp_channel == nullptr) {
      (void)post_assistant_event_locked(
          {AssistantEventType::ReopenMqttAudioChannel, "正在重新打开音频通道",
           "自动连接", true});
    }
  }
}

void handle_wake_word_start_requested(const char *wake_word, uint32_t generation) {
  // A wake request may already be queued when the user mutes the microphone.
  // Recheck policy at the consumer boundary so that stale audio can never
  // reopen a listening session after mute has been acknowledged.
  if (!lunaredu::pet::conversation::assistant_microphone_enabled()) {
    conversation_release_audio_focus("wake_word_start_rejected");
    return;
  }
  std::lock_guard<std::mutex> lock(g_runtime.mutex);
  if (generation != g_runtime.conversation.generation) return;
  if (!assistant_network_connected()) {
    g_runtime.diagnostics.last_error.clear();
    conversation_emit_status("已唤醒", "请先完成 Wi-Fi 配网",
                              wake_word == nullptr ? "" : wake_word, nullptr);
    conversation_release_audio_focus("wake_word_network_unavailable");
    return;
  }
  if (!g_runtime.activation.ready) {
    g_runtime.diagnostics.last_error.clear();
    conversation_emit_status(
        g_runtime.activation.pending ? "等待激活" : "连接中",
        g_runtime.activation.pending ? "请先完成平台账号绑定"
                                     : "正在获取 LunarLink 配置",
        g_runtime.activation.code.c_str(), nullptr);
    conversation_release_audio_focus("wake_word_activation_unavailable");
    return;
  }
  g_runtime.conversation.cancelled = false;
  g_runtime.conversation.listening_mode = "manual";
  g_runtime.conversation.session_close_deadline_ms = 0;

  set_session_has_announcement_locked(false);
  g_runtime.transport.session_id.clear();
  g_runtime.close_audio_channel_requested.store(true);

  set_session_start_inflight_locked(true);
  const ConnectServiceAction action = derive_connect_service_action_locked();
  if (!apply_connect_service_action_locked(action, "wake_word_start_failed")) {
    set_session_start_inflight_locked(false);
    conversation_release_audio_focus("wake_word_start_failed");
    return;
  }
  conversation_emit_status("已唤醒", "请开始说话", wake_word, nullptr);
}

void handle_manual_start_requested(const char *source, uint32_t generation,
                                   bool preserve_session = false) {
  if (!lunaredu::pet::conversation::assistant_microphone_enabled()) {
    conversation_emit_status("麦克风已关闭", "请先在快捷控制中开启麦克风",
                           source == nullptr ? "manual" : source, nullptr);
    return;
  }
  std::unique_lock<std::mutex> lock(g_runtime.mutex);
  if (generation != g_runtime.conversation.generation) return;
  if (!assistant_network_connected()) {
    g_runtime.diagnostics.last_error.clear();
    conversation_emit_status("未联网", "请先完成 Wi-Fi 配网",
                              source == nullptr ? "manual" : source,
                              nullptr);
    return;
  }
  if (!g_runtime.activation.ready) {
    g_runtime.diagnostics.last_error.clear();
    conversation_emit_status(
        g_runtime.activation.pending ? "等待激活" : "连接中",
        g_runtime.activation.pending ? "请先完成平台账号绑定"
                                     : "正在获取 LunarLink 配置",
        g_runtime.activation.code.c_str(), nullptr);
    return;
  }
  if (session_start_inflight_locked() || session_connect_inflight_locked()) {
    conversation_emit_status("连接中", "助手正在准备语音通道",
                              source == nullptr ? "manual" : source, nullptr);
    return;
  }
  conversation_emit_status("已唤醒", "正在切换到语音模式",
                         source == nullptr ? "manual" : source, nullptr);
  lock.unlock();
  if (!conversation_request_audio_focus("manual_start", 500)) {
    lock.lock();
    ESP_LOGW(kTag, "audio focus request failed before manual listening");
    g_runtime.diagnostics.last_error = "assistant_audio_focus_unavailable";
    conversation_emit_status("音频忙", "音乐未能安全暂停，语音对话未开始",
                              source == nullptr ? "manual" : source,
                              g_runtime.diagnostics.last_error.c_str());
    return;
  }
  lock.lock();
  if (generation != g_runtime.conversation.generation ||
      !lunaredu::pet::conversation::assistant_microphone_enabled() ||
      !assistant_network_connected() || !g_runtime.activation.ready ||
      session_start_inflight_locked() || session_connect_inflight_locked()) {
    lock.unlock();
    conversation_release_audio_focus("manual_start_state_changed");
    conversation_emit_status("语音未启动", "启动条件在音频切换期间发生变化",
                              source == nullptr ? "manual" : source, nullptr);
    return;
  }

  g_runtime.conversation.cancelled = false;
  g_runtime.conversation.listening_mode = "manual";
  g_runtime.conversation.session_close_deadline_ms = 0;
  if (!preserve_session) {
    set_session_has_announcement_locked(false);
    g_runtime.transport.session_id.clear();
    g_runtime.close_audio_channel_requested.store(true);
  }

  set_session_start_inflight_locked(true);
  const ConnectServiceAction action = derive_connect_service_action_locked();
  if (!apply_connect_service_action_locked(action, "manual_start_failed")) {
    set_session_start_inflight_locked(false);
    lock.unlock();
    conversation_release_audio_focus("manual_start_failed");
    lock.lock();
    conversation_emit_status("连接异常", "手动收音启动失败",
                              g_runtime.diagnostics.last_error.c_str(),
                              nullptr);
    return;
  }
  conversation_emit_status("正在听", "请开始说话",
                            source == nullptr ? "manual" : source, nullptr);
}

void handle_auto_stop_requested() {
  bool ok = false;
  bool manual_single_turn = false;
  {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    if (!session_listening_locked()) {
      ESP_LOGI(kTag,
               "auto stop ignored state=%s mode=%s session=%s",
               assistant_session_state_name(g_runtime.session_state),
               g_runtime.conversation.listening_mode.c_str(),
               g_runtime.transport.session_id.empty()
                   ? "-"
                   : g_runtime.transport.session_id.c_str());
      return;
    }
    g_runtime.vad.auto_stops += 1;
    manual_single_turn = g_runtime.conversation.listening_mode == "manual";
  }
  ok = stop_listening_now_locked(
      single_turn_stop_reason(manual_single_turn ? "manual" : "auto"),
      false, manual_single_turn ? "手动单轮输入已结束" :
                                 "auto short utterance done");
  if (!ok) {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    g_runtime.diagnostics.last_error = "auto_stop_failed";
  }
}

void handle_listening_no_speech_timeout_due() {
  std::string stop_message;
  bool should_send_stop = false;
  {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    if (!should_timeout_empty_single_turn(
            g_runtime.conversation.listening_mode.c_str(),
            g_runtime.session_state == AssistantSessionState::Listening,
            g_runtime.conversation.listening_started_at_ms,
            g_runtime.vad.utterance_started, now_ms(),
            kAutoConversationNoSpeechTimeoutMs)) {
      return;
    }

    g_runtime.conversation.last_stop_result = "no_speech_timeout";
    g_runtime.conversation.awaiting_response = false;
    g_runtime.conversation.tts_stop_waiting_for_playback = false;
    g_runtime.conversation.tts_stop_playback_grace_deadline_ms = 0;
    if (session_control_channel_connected_locked() &&
        session_identity_available_locked()) {
      stop_message =
          xiaozhi_build_listen_stop_message(g_runtime.transport.session_id);
      should_send_stop = true;
    }
    ESP_LOGI(kTag,
             "conversation ending reason=no_speech_timeout timeout_ms=%u mode=%s standby=%s session=%s state=%s send_stop=%d",
             static_cast<unsigned>(kAutoConversationNoSpeechTimeoutMs),
             g_runtime.conversation.listening_mode.c_str(),
             standby_listen_mode_name(g_runtime.conversation.standby_mode),
             g_runtime.transport.session_id.empty()
                 ? "-"
                 : g_runtime.transport.session_id.c_str(),
             assistant_session_state_name(g_runtime.session_state),
             should_send_stop);
  }

  if (should_send_stop) {
    (void)send_service_text(stop_message);
  }

  std::lock_guard<std::mutex> lock(g_runtime.mutex);
  g_runtime.close_audio_channel_requested.store(true);
    close_audio_channel_locked("单轮聆听无语音超时",
                             AssistantSessionState::Inactive);
}

void handle_flush_pending_mcp() { flush_pending_mcp_messages(); }

void handle_transport_connected_wait_session() {
  std::lock_guard<std::mutex> lock(g_runtime.mutex);
  if (g_runtime.conversation.cancelled) {
    g_runtime.transport.connected = true;
    return;
  }
  mark_transport_connected_waiting_session_locked();
}

void handle_connect_failure(const AssistantEvent &event) {
  std::lock_guard<std::mutex> lock(g_runtime.mutex);
  enter_idle_after_connect_failure_locked(
      event.text.empty() ? nullptr : event.text.c_str(),
      event.detail.empty() ? nullptr : event.detail.c_str(), event.flag);
  if (event.text == "service_udp_connect_failed") {
    conversation_emit_status("连接异常", "UDP 音频通道连接失败",
                              "等待自动重连", nullptr);
  }
}

void handle_transport_disconnected(const char *message) {
  std::lock_guard<std::mutex> lock(g_runtime.mutex);
  const bool should_reconnect =
      assistant_network_connected() && g_runtime.activation.ready;
  if (should_reconnect) {
    set_session_state_locked(AssistantSessionState::Failed);
  }
  reset_session_locked(should_reconnect ? AssistantSessionState::Failed
                                        : AssistantSessionState::Inactive);
  if (should_reconnect) {
    conversation_emit_status("连接中",
                              message == nullptr ? "连接已断开，正在重连" : message,
                              "自动重连", nullptr);
  } else {
    conversation_emit_status("已就绪", "等待网络恢复", "", nullptr);
  }
}

void handle_transport_error(const char *message) {
  std::lock_guard<std::mutex> lock(g_runtime.mutex);
  const bool should_reconnect =
      assistant_network_connected() && g_runtime.activation.ready;
  if (should_reconnect) {
    set_session_state_locked(AssistantSessionState::Failed);
  }
  reset_session_locked(should_reconnect ? AssistantSessionState::Failed
                                        : AssistantSessionState::Inactive);
  if (should_reconnect) {
    conversation_emit_status("连接中", "连接异常，正在重连", "自动重连", message);
  } else {
    conversation_emit_status("已就绪", "等待网络恢复", "", message);
  }
}

void handle_session_hello_received() {
  std::string pending_test_message;
  {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    if (g_runtime.conversation.cancelled) return;
    g_runtime.transport.connected = true;
    set_session_has_announcement_locked(true);
    enter_idle_ready_state_locked("已连接助手", nullptr);
    // send_test_message consumes the pending marker. Leaving it in place until
    // then lets the send path distinguish a recovery retry from a new test and
    // preserve the retry budget across the channel rebuild.
    if (session_audio_path_ready_locked()) {
      pending_test_message = g_runtime.conversation.pending_test_message;
      if (pending_test_message.empty() &&
          !g_runtime.conversation.one_shot_test_active) {
        pending_test_message = g_runtime.conversation.one_shot_test_text;
      }
    }
    ESP_LOGI(kTag,
             "session hello pending_test=%d audio_ready=%d one_shot_active=%d",
             (g_runtime.conversation.pending_test_message.empty() &&
              g_runtime.conversation.one_shot_test_text.empty())
                 ? 0
                 : 1,
             session_audio_path_ready_locked() ? 1 : 0,
             g_runtime.conversation.one_shot_test_active ? 1 : 0);
  }
  if (!pending_test_message.empty() &&
      !lunaredu::pet::conversation::send_test_message(pending_test_message.c_str())) {
    ESP_LOGW(kTag, "queued XiaoZhi test message could not be sent");
  }
}

void handle_audio_channel_ready() {
  {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    if (!session_ready_locked()) {
      return;
    }
    std::string pending_test_message =
        g_runtime.conversation.pending_test_message;
    if (pending_test_message.empty() &&
        !g_runtime.conversation.one_shot_test_active) {
      pending_test_message = g_runtime.conversation.one_shot_test_text;
    }
    if (!pending_test_message.empty()) {
      // Opus encoding has a deep stack footprint. The network executor has an
      // intentionally small internal stack, so ask the 24 KiB audio worker to
      // establish the UDP endpoint and report completion asynchronously.
      g_runtime.conversation.prime_uplink_requested = true;
    }
  }
}

void handle_audio_uplink_primed(bool primed) {
  std::string pending_test_message;
  {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    if (!session_ready_locked()) {
      return;
    }
    pending_test_message = g_runtime.conversation.pending_test_message;
    if (pending_test_message.empty() &&
        !g_runtime.conversation.one_shot_test_active) {
      pending_test_message = g_runtime.conversation.one_shot_test_text;
    }
  }
  if (!pending_test_message.empty()) {
    if (!primed) {
      ESP_LOGW(kTag, "could not prime UDP audio endpoint for queued test");
    }
    if (!lunaredu::pet::conversation::send_test_message(
            pending_test_message.c_str())) {
      ESP_LOGW(
          kTag,
          "queued XiaoZhi test message could not be sent after UDP ready");
    }
  }
}

void handle_continuous_idle_timeout_due() {
  bool reinitialize_wake_frontend = false;
  {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    if (g_runtime.conversation.standby_mode !=
            StandbyListenMode::ContinuousConversation ||
        !session_idle_available_locked() ||
        g_runtime.conversation.idle_entered_at_ms <= 0 ||
        (now_ms() - g_runtime.conversation.idle_entered_at_ms) <
            static_cast<int64_t>(kContinuousConversationIdleTimeoutMs)) {
      return;
    }

    reinitialize_wake_frontend =
        wake_frontend_needs_wake_word_reinit_locked();
    apply_standby_listen_mode_locked(StandbyListenMode::WakeWord, true);
    g_runtime.conversation.listening_mode = "auto";
    g_runtime.conversation.last_stop_result = "连续对话静默超时";
    conversation_emit_status("待唤醒", "连续对话已暂停",
                           "长时间未检测到语音，请说唤醒词", nullptr);
    ESP_LOGI(kTag,
             "continuous standby fell back to wake word after %" PRIi64 " ms",
             static_cast<int64_t>(kContinuousConversationIdleTimeoutMs));
  }
  request_wake_frontend_mode_reinit(reinitialize_wake_frontend);
}

void handle_tts_started() {
  std::lock_guard<std::mutex> lock(g_runtime.mutex);
  if (!g_runtime.conversation.accepts_response(g_runtime.session_state)) return;
  g_runtime.conversation.awaiting_response = false;
  const bool entering_speaking =
      g_runtime.session_state != AssistantSessionState::Speaking ||
      g_runtime.conversation.speaking_started_at_ms <= 0;
  enter_speaking_state_locked(entering_speaking, "助手正在回复",
                              g_runtime.diagnostics.last_message_summary.c_str());
}

void handle_tts_stopped() {
  std::lock_guard<std::mutex> lock(g_runtime.mutex);
  if (g_runtime.conversation.cancelled ||
      g_runtime.session_state == AssistantSessionState::Listening) return;
  g_runtime.conversation.tts_stop_waiting_for_playback = true;
  if (g_runtime.conversation.tts_stop_playback_grace_deadline_ms <= 0) {
    g_runtime.conversation.tts_stop_playback_grace_deadline_ms =
        now_ms() + static_cast<int64_t>(kTtsStopPlaybackDrainGraceMs);
  }
  ESP_LOGI(kTag,
           "tts stop received mode=%s standby=%s session=%s state=%s queued_playback_check=1",
           g_runtime.conversation.listening_mode.c_str(),
           standby_listen_mode_name(g_runtime.conversation.standby_mode),
           g_runtime.transport.session_id.empty()
               ? "-"
               : g_runtime.transport.session_id.c_str(),
           assistant_session_state_name(g_runtime.session_state));
  maybe_post_audio_playback_finished_locked("tts_stop");
}

void handle_audio_playback_finished() {
  std::unique_lock<std::mutex> lock(g_runtime.mutex);
  if (g_runtime.conversation.cancelled ||
      g_runtime.session_state == AssistantSessionState::Listening ||
      !session_identity_available_locked()) {
    ESP_LOGI(kTag,
             "audio playback finished ignored state=%s mode=%s session=%s",
             assistant_session_state_name(g_runtime.session_state),
             g_runtime.conversation.listening_mode.c_str(),
             g_runtime.transport.session_id.empty()
                 ? "-"
                 : g_runtime.transport.session_id.c_str());
    return;
  }
  g_runtime.conversation.tts_stop_waiting_for_playback = false;
  g_runtime.conversation.tts_stop_playback_grace_deadline_ms = 0;
  const bool realtime = g_runtime.conversation.listening_mode == "realtime";
  const bool automatic_resume_ready = should_resume_after_response(
      g_runtime.conversation.listening_mode.c_str(),
      conversation_auto_resume_enabled_locked() && assistant_network_connected() &&
          g_runtime.activation.ready && session_identity_available_locked() &&
          session_control_path_ready_locked());
  const auto completion = lunaredu::pet::conversation::conversation_response_completion(
      automatic_resume_ready,
      g_runtime.conversation.one_shot_test_active,
      lunaredu::pet::conversation::assistant_microphone_enabled());
  const bool should_resume = completion ==
      lunaredu::pet::conversation::ConversationResponseCompletion::ResumeListening;
  const bool audio_ready = session_audio_path_ready_locked();

  ESP_LOGI(kTag,
           "audio playback finished event mode=%s standby=%s session=%s state=%s resume_listening=%d audio_ready=%d",
           g_runtime.conversation.listening_mode.c_str(),
           standby_listen_mode_name(g_runtime.conversation.standby_mode),
           g_runtime.transport.session_id.empty()
               ? "-"
               : g_runtime.transport.session_id.c_str(),
           assistant_session_state_name(g_runtime.session_state),
           should_resume, audio_ready);

  const uint32_t generation = g_runtime.conversation.generation;
  lock.unlock();
  flush_pending_mcp_messages();
  // Restoring the shared bus clock can wait for the output owner. Keep UI
  // snapshots and cancellation responsive while it acknowledges that change.
  lunaredu::pet::conversation::audio_io::enable_output(false);
  lunaredu::pet::conversation::audio_io::clear_output_reference();
  lock.lock();
  if (generation != g_runtime.conversation.generation ||
      g_runtime.conversation.control_pending) return;
  g_runtime.conversation.awaiting_response = false;

  if (completion ==
      lunaredu::pet::conversation::ConversationResponseCompletion::EndOneShotTest) {
    g_runtime.conversation.one_shot_test_active = false;
    g_runtime.conversation.pending_test_message.clear();
    g_runtime.conversation.pending_test_is_audio_retry = false;
    g_runtime.conversation.one_shot_test_text.clear();
    g_runtime.conversation.one_shot_test_audio_retry_count = 0;
    g_runtime.close_audio_channel_requested.store(true);
    close_audio_channel_locked("你好测试已结束",
                               AssistantSessionState::Inactive);
    return;
  }

  if (should_resume && realtime) {
    enter_realtime_listening_state_locked(
        "实时会话中", g_runtime.diagnostics.last_message_summary.c_str());
    return;
  }

  if (should_resume) {
    g_runtime.conversation.listening_mode = "auto";
    set_session_start_inflight_locked(true);
    g_runtime.conversation.session_close_deadline_ms = 0;
    if (audio_ready) {
      (void)post_assistant_event_locked(
          {AssistantEventType::StartListeningRequested, {}, {}, false});
    } else {
      (void)post_assistant_event_locked(
          {AssistantEventType::ReopenMqttAudioChannel, "正在恢复连续收音",
           "连续对话", false});
    }
    conversation_emit_status("正在听", "准备继续对话",
                              "无需再次唤醒", nullptr);
    return;
  }

  close_audio_channel_locked("已播报完成", AssistantSessionState::ReadyIdle);
  conversation_release_audio_focus("one_shot_response_finished");
}

void handle_stt_activity_received(const char *summary) {
  std::lock_guard<std::mutex> lock(g_runtime.mutex);
  if (!session_listening_locked()) {
    return;
  }
  // Show streaming STT text on screen
  const char *display_text = summary == nullptr ? "" : summary;
  if (display_text[0] != '\0') {
    conversation_emit_status("正在听", display_text, "语音识别中", nullptr);
  } else {
    conversation_emit_status("正在听", "已收到语音", "语音识别中", nullptr);
  }
}

void handle_session_goodbye() {
  std::lock_guard<std::mutex> lock(g_runtime.mutex);
  g_runtime.conversation.last_stop_result = "goodbye";
  g_runtime.close_audio_channel_requested.store(true);
  close_audio_channel_locked("会话已结束", AssistantSessionState::Inactive);
}

void handle_start_listening_succeeded() {
  uint32_t generation = 0;
  std::string start_message;
  bool should_send_start = false;
  bool should_reopen_audio = false;
  {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    if (!g_runtime.conversation.cancelled && session_should_send_listen_start_locked()) {
      generation = g_runtime.conversation.generation;
      const PrepareStartListeningResult result =
          prepare_start_listening_locked(start_message);
      should_send_start = result == PrepareStartListeningResult::ReadyToSend;
      should_reopen_audio =
          result == PrepareStartListeningResult::ReopenMqttAudioChannel;
      if (should_reopen_audio) {
        (void)post_assistant_event_locked(
            {AssistantEventType::ReopenMqttAudioChannel, "正在准备语音会话",
             "自动连接", false});
        enter_connecting_state_locked("正在准备语音会话", "自动连接", false);
      }
    }
  }
  if (should_reopen_audio || !should_send_start) {
    return;
  }

  const bool ok = send_service_text(start_message);
  std::string muted_stop_message;
  {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    if (generation != g_runtime.conversation.generation || g_runtime.conversation.cancelled) {
      return; // The control owner sends stop/abort for the invalidated start.
    }
    if (ok && lunaredu::pet::conversation::assistant_microphone_enabled()) {
      commit_start_listening_success_locked();
    } else {
      commit_start_listening_failure_locked();
      if (ok && session_identity_available_locked()) {
        muted_stop_message =
            xiaozhi_build_listen_stop_message(g_runtime.transport.session_id);
      }
    }
  }
  if (!muted_stop_message.empty()) {
    (void)send_service_text(muted_stop_message);
  }
}

void handle_conversation_control(const AssistantEvent &event) {
  // Wait for the current bounded audio iteration outside the runtime mutex.
  // The request already invalidated its generation and blocked new downlink.
  quiesce_audio();
  std::string stop, abort;
  uint32_t generation;
  bool preserve_session;
  {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    if (event.generation != g_runtime.conversation.generation) return;
    if (session_identity_available_locked()) {
      stop = xiaozhi_build_listen_stop_message(g_runtime.transport.session_id);
      abort = xiaozhi_build_abort_message(g_runtime.transport.session_id);
    }
    preserve_session = session_identity_available_locked() && session_audio_path_ready_locked();
    g_runtime.pending_mcp_messages.clear();
    close_audio_channel_locked(event.flag ? "重新聆听" : "已取消对话",
                              AssistantSessionState::ReadyIdle, !event.flag);
    generation = g_runtime.conversation.generation;
    g_runtime.conversation.control_pending = event.flag;
  }
  // End old cloud input/output before issuing a new listen on the same session.
  if (!stop.empty()) (void)send_service_text(stop);
  if (!abort.empty()) (void)send_service_text(abort);
  if (event.flag) {
    handle_manual_start_requested("left_hold", generation, preserve_session);
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    if (generation == g_runtime.conversation.generation) {
      g_runtime.conversation.control_pending = false;
      if (!session_start_inflight_locked() && !session_listening_locked())
        conversation_release_audio_focus("restart_listening_failed");
    }
  }
}

void handle_abort_speaking_requested() {
  std::string message;
  {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    if (!session_can_abort_speaking_locked()) {
      conversation_emit_status("待唤醒", "当前没有可停止的回答",
                                "请说唤醒词", nullptr);
      return;
    }
    message = xiaozhi_build_abort_message(g_runtime.transport.session_id);
    g_runtime.close_audio_channel_requested.store(true);
  }

  const bool ok = send_service_text(message);
  std::lock_guard<std::mutex> lock(g_runtime.mutex);
  if (!ok) {
    g_runtime.diagnostics.last_error = "manual_abort_failed";
    conversation_emit_status("连接异常", "停止回答失败",
                              g_runtime.diagnostics.last_error.c_str(),
                              nullptr);
    return;
  }
  close_audio_channel_locked("已停止回答", AssistantSessionState::ReadyIdle);
}

void handle_set_standby_mode(StandbyListenMode requested_mode) {
  bool should_stop_active_session = false;
  bool reinitialize_wake_frontend = false;
  {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    if (requested_mode == StandbyListenMode::WakeWord) {
      reinitialize_wake_frontend =
          wake_frontend_needs_wake_word_reinit_locked();
    }
    apply_standby_listen_mode_locked(requested_mode, true);
    if (requested_mode == StandbyListenMode::ContinuousConversation) {
      g_runtime.conversation.listening_mode = "auto";
      g_runtime.conversation.last_stop_result = "切到连续对话";
      conversation_emit_status("连续对话", "已切到连续对话模式",
                                "将自动开始连续短句对话", nullptr);
    } else {
      g_runtime.conversation.last_stop_result = "切到唤醒词";
      should_stop_active_session = session_listening_locked() ||
          g_runtime.session_state == AssistantSessionState::Speaking ||
          session_active_locked();
      conversation_emit_status("待唤醒", "已切到唤醒词模式",
                                "请说唤醒词", nullptr);
    }
  }
  if (should_stop_active_session) {
    stop_listening_now_locked("mode_switch", false, "已切到唤醒词模式");
  }
  request_wake_frontend_mode_reinit(reinitialize_wake_frontend);
}

void handle_toggle_standby_mode() {
  StandbyListenMode next_mode;
  {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    next_mode = g_runtime.conversation.standby_mode == StandbyListenMode::WakeWord
                    ? StandbyListenMode::ContinuousConversation
                    : StandbyListenMode::WakeWord;
  }
  handle_set_standby_mode(next_mode);
}

void handle_reopen_mqtt_audio_channel(const AssistantEvent &event) {
  flush_pending_mcp_messages();
  {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    set_session_has_announcement_locked(false);
    enter_connecting_state_locked(
        event.text.empty() ? "正在重新打开音频通道" : event.text.c_str(),
        event.detail.empty() ? "自动连接" : event.detail.c_str(), event.flag);
  }
  (void)reopen_mqtt_audio_channel();
}

void handle_wake_word_unavailable(const char *wake_word) {
  std::lock_guard<std::mutex> lock(g_runtime.mutex);
  conversation_emit_status("唤醒词异常", "当前唤醒词资源不可用",
                            wake_word == nullptr ? "" : wake_word, nullptr);
}

void handle_local_audio_input_failed(const char *error) {
  std::lock_guard<std::mutex> lock(g_runtime.mutex);
  conversation_emit_status("音频异常", "板载麦克风启用失败", "",
                            error == nullptr ? nullptr : error);
}

void handle_emotion_received(const char *emotion) {
  std::lock_guard<std::mutex> lock(g_runtime.mutex);
  if (emotion != nullptr && strlen(emotion) > 0) {
    ESP_LOGI(kTag, "Emotion received: %s", emotion);
    conversation_emit_emotion(emotion);
  }
}

void dispatch_event(const AssistantEvent &event) {
  switch (event.type) {
  case AssistantEventType::ConnectRequested:
    handle_connect_requested(event);
    break;
  case AssistantEventType::ContinuousStartRequested:
    handle_continuous_start_requested();
    break;

  case AssistantEventType::StaleSessionCloseDue:
    handle_stale_session_close_due();
    break;
  case AssistantEventType::ConnectionWatchdogDue:
    handle_connection_watchdog_due();
    break;
  case AssistantEventType::WakeWordStartRequested:
    handle_wake_word_start_requested(event.text.c_str(), event.generation);
    break;
  case AssistantEventType::AutoStopRequested:
    handle_auto_stop_requested();
    break;
  case AssistantEventType::FlushPendingMcp:
    handle_flush_pending_mcp();
    break;
  case AssistantEventType::TransportConnectedWaitSession:
    handle_transport_connected_wait_session();
    break;
  case AssistantEventType::ConnectFailure:
    handle_connect_failure(event);
    break;
  case AssistantEventType::TransportDisconnected:
    handle_transport_disconnected(event.text.c_str());
    break;
  case AssistantEventType::TransportError:
    handle_transport_error(event.text.c_str());
    break;
  case AssistantEventType::SessionHelloReceived:
    handle_session_hello_received();
    break;
  case AssistantEventType::AudioChannelReady:
    handle_audio_channel_ready();
    break;
  case AssistantEventType::AudioUplinkPrimed:
    handle_audio_uplink_primed(event.flag);
    break;
  case AssistantEventType::TtsStarted:
    handle_tts_started();
    break;
  case AssistantEventType::TtsStopped:
    handle_tts_stopped();
    break;
  case AssistantEventType::AudioPlaybackFinished:
    handle_audio_playback_finished();
    break;
  case AssistantEventType::SttActivityReceived:
    handle_stt_activity_received(event.text.c_str());
    break;
  case AssistantEventType::EmotionReceived:
    handle_emotion_received(event.text.c_str());
    break;
  case AssistantEventType::ListeningNoSpeechTimeoutDue:
    handle_listening_no_speech_timeout_due();
    break;
  case AssistantEventType::ContinuousIdleTimeoutDue:
    handle_continuous_idle_timeout_due();
    break;
  case AssistantEventType::SessionGoodbye:
    handle_session_goodbye();
    break;
  case AssistantEventType::StartListeningRequested:
    handle_start_listening_succeeded();
    break;
  case AssistantEventType::ManualStartRequested:
    handle_manual_start_requested(event.text.c_str(), event.generation);
    break;
  case AssistantEventType::ConversationControlRequested:
    handle_conversation_control(event);
    break;
  case AssistantEventType::AbortSpeakingRequested:
    handle_abort_speaking_requested();
    break;
  case AssistantEventType::ToggleStandbyMode:
    handle_toggle_standby_mode();
    break;
  case AssistantEventType::SetStandbyMode:
    handle_set_standby_mode(event.flag ? StandbyListenMode::ContinuousConversation
                                       : StandbyListenMode::WakeWord);
    break;
  case AssistantEventType::ReopenMqttAudioChannel:
    handle_reopen_mqtt_audio_channel(event);
    break;
  case AssistantEventType::WakeWordUnavailable:
    handle_wake_word_unavailable(event.text.c_str());
    break;
  case AssistantEventType::LocalAudioInputFailed:
    handle_local_audio_input_failed(event.text.c_str());
    break;
  default:
    break;
  }
}

} // namespace

void setup_assistant_loop() {
  std::lock_guard<std::mutex> lock(g_runtime.mutex);
  g_runtime.pending_events.clear();
}

bool post_assistant_event_locked(AssistantEvent event) {
  if (event_requires_microphone(event.type) &&
      !lunaredu::pet::conversation::assistant_microphone_enabled()) {
    return false;
  }
  if (g_runtime.pending_events.size() >= kAssistantEventQueueCapacity) {
    g_runtime.diagnostics.last_error = "assistant_event_queue_full";
    return false;
  }
  event.generation = g_runtime.conversation.generation;
  g_runtime.pending_events.push_back(std::move(event));
  return true;
}

bool request_conversation_control(bool listen) {
  std::lock_guard<std::mutex> lock(g_runtime.mutex);
  if (!g_runtime.service_initialized) return false;
  if (!listen && !g_runtime.conversation.control_pending &&
      !session_start_inflight_locked() && !g_runtime.conversation.awaiting_response &&
      !g_runtime.conversation.one_shot_test_active &&
      g_runtime.session_state != AssistantSessionState::Listening &&
      g_runtime.session_state != AssistantSessionState::Speaking) return true;
  if (listen && (!g_runtime.service_enabled || g_runtime.service_suspended ||
                 !lunaredu::pet::conversation::assistant_microphone_enabled())) return false;
  if (listen && !g_runtime.conversation.cancelled &&
      (g_runtime.session_state == AssistantSessionState::Listening ||
       (g_runtime.session_state == AssistantSessionState::Opening &&
        session_start_inflight_locked()))) return true;
  // Explicit controls take priority over bounded background work. Supersede
  // older queued events, including a start immediately followed by Back.
  ++g_runtime.conversation.generation;
  g_runtime.conversation.cancelled = true;
  g_runtime.conversation.control_pending = true;
  for (auto event = g_runtime.pending_events.begin(); event != g_runtime.pending_events.end();) {
    if (assistant_event_tracks_turn(event->type)) event = g_runtime.pending_events.erase(event);
    else ++event;
  }
  // Reserve a control slot even when background transport notifications filled
  // the queue. A user cancellation must never be silently rejected.
  if (g_runtime.pending_events.size() >= kAssistantEventQueueCapacity)
    g_runtime.pending_events.pop_back();
  AssistantEvent event{AssistantEventType::ConversationControlRequested, {}, {}, listen};
  event.generation = g_runtime.conversation.generation;
  g_runtime.pending_events.push_front(std::move(event));
  return true;
}

bool post_assistant_event(AssistantEvent event) {
  std::lock_guard<std::mutex> lock(g_runtime.mutex);
  return post_assistant_event_locked(std::move(event));
}

void apply_microphone_policy_change(bool enabled) {
  if (!enabled) {
    (void)lunaredu::pet::conversation::audio_io::enable_input(false);
  }

  bool runtime_initialized = false;
  bool show_muted_status = false;
  bool start_wake_frontend = false;
  {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    runtime_initialized = g_runtime.service_initialized;
    if (!runtime_initialized) {
      return;
    }

    if (!enabled) {
      set_session_start_inflight_locked(false);
      g_runtime.conversation.prime_uplink_requested = false;
      for (auto event = g_runtime.pending_events.begin();
           event != g_runtime.pending_events.end();) {
        if (event_requires_microphone(event->type)) {
          event = g_runtime.pending_events.erase(event);
        } else {
          ++event;
        }
      }
      show_muted_status =
          g_runtime.session_state != AssistantSessionState::Listening &&
          g_runtime.session_state != AssistantSessionState::Speaking;
    } else {
      start_wake_frontend =
          g_runtime.service_enabled && !g_runtime.service_suspended &&
          g_runtime.wake_word.afe_initialized &&
          g_runtime.session_state != AssistantSessionState::Listening &&
          g_runtime.session_state != AssistantSessionState::Speaking;
    }
  }

  {
    std::lock_guard<std::mutex> wake_lock(g_runtime.wake_word_mutex);
    if (enabled) {
      if (start_wake_frontend) {
        g_runtime.wake_word_frontend.start();
      }
    } else {
      g_runtime.wake_word_frontend.stop();
    }
  }

  if (!enabled) {
    stop_listening_for_microphone_mute();
    if (show_muted_status) {
      conversation_emit_status("麦克风已关闭", "不会采集或上传声音",
                             "可在快捷控制中重新开启", nullptr);
    }
  }
}

void tick_assistant_loop() {
  {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    enqueue_periodic_events_locked(now_ms());
  }
  for (;;) {
    AssistantEvent event;
    {
      std::lock_guard<std::mutex> lock(g_runtime.mutex);
      if (g_runtime.pending_events.empty()) {
        break;
      }
      event = std::move(g_runtime.pending_events.front());
      g_runtime.pending_events.pop_front();
      if (assistant_event_tracks_turn(event.type) &&
          event.generation != g_runtime.conversation.generation) continue;
    }
    dispatch_event(event);
  }
}

} // namespace lunaredu::pet::conversation::internal
