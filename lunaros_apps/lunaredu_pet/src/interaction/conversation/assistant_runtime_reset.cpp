#include "interaction/voice/resident_audio.h"
#include "interaction/conversation/assistant_core.h"
#include "lunaredu_audio_bridge.h"
#include "interaction/conversation/host_events.h"

namespace lunaredu::pet::conversation::internal {

void reset_session_locked(AssistantSessionState next_session_state) {
  const AssistantSessionState previous = g_runtime.session_state;
  const std::string previous_session_id = g_runtime.transport.session_id;
  g_runtime.session_state = next_session_state;
  g_runtime.transport.connected = false;
  session_reset_supporting_facts_locked();
  g_runtime.transport.connect_started_at_ms = 0;
  g_runtime.conversation.idle_entered_at_ms = now_ms();
  g_runtime.conversation.listening_started_at_ms = 0;
  g_runtime.conversation.speaking_started_at_ms = 0;
  g_runtime.conversation.awaiting_response = false;
  g_runtime.conversation.one_shot_test_active = false;
  g_runtime.conversation.prime_uplink_requested = false;
  if (g_runtime.conversation.pending_test_message.empty()) {
    g_runtime.conversation.pending_test_is_audio_retry = false;
    g_runtime.conversation.one_shot_test_text.clear();
    g_runtime.conversation.one_shot_test_audio_retry_count = 0;
  }
  g_runtime.conversation.tts_stop_waiting_for_playback = false;
  g_runtime.conversation.tts_stop_playback_grace_deadline_ms = 0;
  g_runtime.conversation.downlink_audio_active = false;
  // In continuous conversation mode, don't add delay for wake word resume
  if (g_runtime.conversation.standby_mode == StandbyListenMode::ContinuousConversation) {
    g_runtime.conversation.wake_word_resume_not_before_ms = 0;
  } else {
    g_runtime.conversation.wake_word_resume_not_before_ms =
        g_runtime.conversation.idle_entered_at_ms +
        static_cast<int64_t>(kWakeWordResumeDelayMs);
  }
  g_runtime.transport.session_id.clear();
  g_runtime.pending_mcp_messages.clear();
  g_runtime.conversation.session_close_deadline_ms = 0;

  drain_downlink_audio_queue();
  if (g_runtime.transport.udp_channel != nullptr) {
    // The UDP receive callback also takes g_runtime.mutex. Disconnect() waits
    // for that receive task to exit, so doing it from this locked function can
    // deadlock with a callback that already received its final datagram and is
    // waiting for this mutex. The connect owner moves and closes the channel
    // outside the mutex on its next iteration.
    g_runtime.close_audio_channel_requested.store(true,
                                                  std::memory_order_release);
  }
  g_runtime.transport.mqtt_udp_channel.reset();
  lunaredu::pet::conversation::audio_io::enable_input(false);
  lunaredu::pet::conversation::audio_io::enable_output(false);
  g_runtime.vad.voice_activity_active = false;
  g_runtime.vad.pcm_frames = 0;
  g_runtime.vad.speech_frames = 0;
  g_runtime.vad.silence_frames = 0;
  g_runtime.vad.last_rms = 0;
  g_runtime.vad.last_peak = 0;
  g_runtime.vad.last_raw_rms = 0;
  g_runtime.vad.last_raw_peak = 0;
  g_runtime.vad.last_raw_mean = 0;
  g_runtime.vad.current_utterance_ms = 0;
  g_runtime.vad.speech_ms = 0;
  g_runtime.vad.silence_ms = 0;
  g_runtime.diagnostics.last_error.clear();
  g_runtime.diagnostics.last_binary_error.clear();
  g_runtime.diagnostics.last_playback_error.clear();
  ESP_LOGI(kTag,
           "conversation reset reason=transport_reset state=%s -> %s session=%s close_udp=1",
           assistant_session_state_name(previous),
           assistant_session_state_name(next_session_state),
           previous_session_id.empty() ? "-" : previous_session_id.c_str());
}

void close_audio_channel_locked(const char *detail,
                                AssistantSessionState next_session_state) {
  const AssistantSessionState previous = g_runtime.session_state;
  const std::string previous_session_id = g_runtime.transport.session_id;
  g_runtime.session_state = next_session_state;
  if (next_session_state == AssistantSessionState::ReadyIdle) {
    set_session_has_announcement_locked(true);
    set_session_connect_inflight_locked(false);
    set_session_start_inflight_locked(false);
  } else {
    session_reset_supporting_facts_locked();
  }
  g_runtime.transport.connect_started_at_ms = 0;
  g_runtime.conversation.idle_entered_at_ms = now_ms();
  g_runtime.conversation.listening_started_at_ms = 0;
  g_runtime.conversation.speaking_started_at_ms = 0;
  g_runtime.conversation.awaiting_response = false;
  g_runtime.conversation.one_shot_test_active = false;
  g_runtime.conversation.prime_uplink_requested = false;
  g_runtime.conversation.pending_test_message.clear();
  g_runtime.conversation.pending_test_is_audio_retry = false;
  g_runtime.conversation.one_shot_test_text.clear();
  g_runtime.conversation.one_shot_test_audio_retry_count = 0;
  g_runtime.conversation.tts_stop_waiting_for_playback = false;
  g_runtime.conversation.tts_stop_playback_grace_deadline_ms = 0;
  g_runtime.conversation.downlink_audio_active = false;
  // In continuous conversation mode, don't add delay for wake word resume
  if (g_runtime.conversation.standby_mode == StandbyListenMode::ContinuousConversation) {
    g_runtime.conversation.wake_word_resume_not_before_ms = 0;
  } else {
    g_runtime.conversation.wake_word_resume_not_before_ms =
        g_runtime.conversation.idle_entered_at_ms +
        static_cast<int64_t>(kWakeWordResumeDelayMs);
  }
  g_runtime.conversation.local_audio_capture_not_before_ms = 0;
  if (next_session_state != AssistantSessionState::ReadyIdle) {
    g_runtime.transport.session_id.clear();
    g_runtime.pending_mcp_messages.clear();
  }
  g_runtime.conversation.session_close_deadline_ms = 0;

  drain_downlink_audio_queue();
  if (next_session_state != AssistantSessionState::ReadyIdle &&
      g_runtime.transport.udp_channel != nullptr) {
    g_runtime.close_audio_channel_requested.store(true,
                                                  std::memory_order_release);
  }
  if (next_session_state != AssistantSessionState::ReadyIdle) {
    g_runtime.transport.mqtt_udp_channel.reset();
  }
  lunaredu::pet::conversation::audio_io::enable_input(false);
  lunaredu::pet::conversation::audio_io::enable_output(false);
  lunaredu::pet::conversation::audio_io::clear_output_reference();
  g_runtime.vad.voice_activity_active = false;
  g_runtime.vad.pcm_frames = 0;
  g_runtime.vad.speech_frames = 0;
  g_runtime.vad.silence_frames = 0;
  g_runtime.vad.last_rms = 0;
  g_runtime.vad.last_peak = 0;
  g_runtime.vad.last_raw_rms = 0;
  g_runtime.vad.last_raw_peak = 0;
  g_runtime.vad.last_raw_mean = 0;
  g_runtime.vad.current_utterance_ms = 0;
  g_runtime.vad.speech_ms = 0;
  g_runtime.vad.silence_ms = 0;
  g_runtime.diagnostics.last_error.clear();
  g_runtime.diagnostics.last_binary_error.clear();
  g_runtime.diagnostics.last_playback_error.clear();
  ESP_LOGI(kTag,
           "conversation end reason=%s state=%s -> %s session=%s close_udp=%d",
           detail == nullptr ? "session_closed" : detail,
           assistant_session_state_name(previous),
           assistant_session_state_name(next_session_state),
           previous_session_id.empty() ? "-" : previous_session_id.c_str(),
           next_session_state != AssistantSessionState::ReadyIdle);
  conversation_emit_status("待唤醒",
                            detail == nullptr ? "会话已结束" : detail,
                            "请说唤醒词", nullptr);
}

bool ensure_opus_encoder_locked() {
  return opus_audio_codec_ensure_encoder(
      g_runtime.codec, static_cast<int>(kBrowserSampleRate),
      static_cast<int>(kFrameDurationMs), static_cast<int>(kBrowserChannels),
      kOpusComplexity, &g_runtime.diagnostics.last_error);
}

bool ensure_opus_decoder_locked(int sample_rate, int frame_duration_ms) {
  if (sample_rate <= 0) {
    sample_rate = g_runtime.transport.server_output_sample_rate;
  }
  if (frame_duration_ms <= 0) {
    frame_duration_ms = g_runtime.transport.server_output_frame_duration_ms;
  }
  return opus_audio_codec_ensure_decoder(
      g_runtime.codec, sample_rate, frame_duration_ms,
      static_cast<int>(kBrowserChannels), &g_runtime.diagnostics.last_error);
}


std::shared_ptr<NetworkMqttSession> take_service_mqtt_locked() {
  if (g_runtime.transport.mqtt_session == nullptr) {
    return nullptr;
  }
  return std::move(g_runtime.transport.mqtt_session);
}

bool warmup_opus_codec_locked() {
  const std::string previous_error = g_runtime.diagnostics.last_error;
  const std::string previous_transport_error =
      g_runtime.diagnostics.last_transport_error;
  const bool ok =
      ensure_opus_encoder_locked() &&
      ensure_opus_decoder_locked(g_runtime.transport.server_output_sample_rate,
                                 g_runtime.transport.server_output_frame_duration_ms);
  if (!ok) {
    ESP_LOGW(kTag, "Opus codec warmup failed: %s",
             g_runtime.diagnostics.last_error.c_str());
    g_runtime.diagnostics.last_error = previous_error;
    g_runtime.diagnostics.last_transport_error = previous_transport_error;
  }
  return ok;
}



void drain_downlink_audio_queue() {
  if (g_runtime.downlink_audio_queue == nullptr) {
    return;
  }

  DownlinkAudioWorkItem *item = nullptr;
  while (xQueueReceive(g_runtime.downlink_audio_queue, &item, 0) == pdTRUE) {
    DownlinkAudioWorkItem::release(item);
    item = nullptr;
  }
}

} // namespace lunaredu::pet::conversation::internal
