#include "interaction/voice/resident_audio.h"
#include "interaction/voice/assistant_audio.h"
#include "interaction/voice/assistant_pcm_statistics.h"
#include "interaction/conversation/assistant_core.h"
#include "interaction/conversation/assistant_session.h"
#include "lunaredu_audio_bridge.h"
#include "interaction/conversation/session_policy.h"
#include "lunaredu_pet_conversation_policy.h"
#include "interaction/voice/local_audio_app.h"
#include "interaction/conversation/host_events.h"
#include "interaction/transport/assistant_transport.h"
#include "interaction/conversation/assistant_loop.h"
#include "interaction/conversation/assistant_turn_policy.h"
#include "interaction/conversation/assistant_lifecycle.h"

#include <cinttypes>
#include <cmath>
#include <cstring>
#include <esp_opus_enc.h>
#include <memory>

namespace lunaredu::pet::conversation::internal {

namespace {

std::mutex g_audio_iteration_mutex;

uint32_t read_udp_sequence(const std::string &datagram) {
  if (datagram.size() < 16) {
    return 0;
  }
  return (static_cast<uint32_t>(static_cast<uint8_t>(datagram[12])) << 24) |
         (static_cast<uint32_t>(static_cast<uint8_t>(datagram[13])) << 16) |
         (static_cast<uint32_t>(static_cast<uint8_t>(datagram[14])) << 8) |
         static_cast<uint32_t>(static_cast<uint8_t>(datagram[15]));
}

} // namespace

void observe_vad_frame_locked(const int16_t *samples, size_t sample_count,
                              uint32_t frame_ms) {
  if (samples == nullptr || sample_count == 0) {
    return;
  }

  uint32_t peak = 0;
  long double sum_square = 0.0;
  for (size_t index = 0; index < sample_count; ++index) {
    const int16_t sample = samples[index];
    peak = std::max(peak, pcm_abs_i16(sample));
    const long double value = static_cast<long double>(sample);
    sum_square += value * value;
  }
  const uint32_t rms = static_cast<uint32_t>(std::sqrt(
      static_cast<double>(sum_square /
                          static_cast<long double>(sample_count))));
  const bool speech_frame = rms >= kVadFrameEnergyThreshold ||
                            peak >= kVadFrameEnergyThreshold * 3U;

  g_runtime.vad.pcm_frames += 1;
  g_runtime.vad.last_rms = rms;
  g_runtime.vad.last_peak = peak;
  if (speech_frame) {
    g_runtime.vad.speech_frames += 1;
    g_runtime.vad.speech_ms += frame_ms;
    g_runtime.vad.silence_ms = 0;
    g_runtime.vad.current_utterance_ms += frame_ms;
  } else {
    g_runtime.vad.silence_frames += 1;
    g_runtime.vad.silence_ms += frame_ms;
    g_runtime.vad.speech_ms = 0;
  }

  if (!g_runtime.vad.voice_activity_active &&
      g_runtime.vad.speech_ms >= kVadSpeechStartMs) {
    g_runtime.vad.voice_activity_active = true;
    g_runtime.vad.utterance_started = true;
  } else if (g_runtime.vad.voice_activity_active &&
             g_runtime.vad.silence_ms >= kVadSpeechEndMs) {
    g_runtime.vad.voice_activity_active = false;
  }
}

bool process_uplink_audio_work_item(const std::vector<int16_t> &samples) {
  if (samples.empty()) {
    return false;
  }

  OpusAudioCodec *codec = nullptr;
  std::string reject_reason;

  int encoder_frame_samples = 0;
  int encoder_output_bytes = 0;
  {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    if (g_runtime.conversation.cancelled || g_runtime.conversation.control_pending ||
        g_runtime.session_state != AssistantSessionState::Listening) {
      g_runtime.vad.speech_ms = 0;
      g_runtime.vad.silence_ms = 0;
      g_runtime.vad.voice_activity_active = false;
      g_runtime.vad.utterance_started = false;
      return false;
    }

    const bool transport_ready = session_ready_locked();
    if (!transport_ready) {
      reject_reason = "service_mqtt_udp_not_open";
      return false;
    }

    if (!ensure_opus_encoder_locked()) {
      return false;
    }

    codec = &g_runtime.codec;
    encoder_frame_samples = g_runtime.codec.encoder_frame_samples;
    encoder_output_bytes = g_runtime.codec.encoder_output_bytes;
  }

  if (codec == nullptr || encoder_frame_samples <= 0 || encoder_output_bytes <= 0) {
    return false;
  }

  const size_t frame_samples = static_cast<size_t>(encoder_frame_samples);
  static std::vector<int16_t> s_pcm_frame;
  static std::vector<uint8_t> s_encoded;
  static std::string s_udp_datagram;

  for (size_t offset = 0; offset < samples.size(); offset += frame_samples) {
    s_pcm_frame.assign(frame_samples, 0);
    const size_t chunk = std::min(frame_samples, samples.size() - offset);
    std::memcpy(s_pcm_frame.data(), samples.data() + offset,
                chunk * sizeof(int16_t));

    s_encoded.resize(static_cast<size_t>(encoder_output_bytes));
    esp_audio_enc_in_frame_t input = {
        .buffer = reinterpret_cast<uint8_t *>(s_pcm_frame.data()),
        .len = static_cast<uint32_t>(s_pcm_frame.size() * sizeof(int16_t)),
    };
    esp_audio_enc_out_frame_t output = {
        .buffer = s_encoded.data(),
        .len = static_cast<uint32_t>(s_encoded.size()),
        .encoded_bytes = 0,
        .pts = 0,
    };
    const int encode_result =
        esp_opus_enc_process(codec->encoder, &input, &output);
    if (encode_result != ESP_AUDIO_ERR_OK || output.encoded_bytes == 0) {
      std::lock_guard<std::mutex> lock(g_runtime.mutex);
      g_runtime.diagnostics.last_transport_error = "opus_encode_failed";
      g_runtime.diagnostics.last_error = g_runtime.diagnostics.last_transport_error;
      return false;
    }

    s_encoded.resize(output.encoded_bytes);
    
    uint32_t timestamp_ms = 0;
    std::shared_ptr<NetworkUdpChannel> udp_channel;
    uint32_t sequence = 0;
    {
      std::lock_guard<std::mutex> lock(g_runtime.mutex);
      if (g_runtime.conversation.cancelled || g_runtime.conversation.control_pending ||
          !session_ready_locked()) {
        g_runtime.diagnostics.last_binary_error = "mqtt_udp_audio_channel_closed";
        g_runtime.diagnostics.last_error = g_runtime.diagnostics.last_binary_error;
        return false;
      }
      // Microphone frames bypass send_service_binary; use the same Mac turn
      // fence here so recording and the endpoint probe share the wire format.
      timestamp_ms = kAssistantUsesMac ? g_runtime.transport.mac_turn_id
                                      : static_cast<uint32_t>(now_ms());
      if (!g_runtime.transport.mqtt_udp_channel.encrypt_payload(
              reinterpret_cast<const uint8_t *>(s_encoded.data()),
              s_encoded.size(), timestamp_ms, s_udp_datagram)) {
        g_runtime.diagnostics.last_binary_error =
            "mqtt_udp_audio_encrypt_failed:" +
            std::to_string(g_runtime.transport.mqtt_udp_channel.last_crypto_error());
        g_runtime.diagnostics.last_error = g_runtime.diagnostics.last_binary_error;
        continue;
      }
      udp_channel = g_runtime.transport.udp_channel;
      sequence = read_udp_sequence(s_udp_datagram);
    }

    const int sent = udp_channel == nullptr ? -1 : udp_channel->Send(s_udp_datagram);
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    if (sent <= 0) {
      g_runtime.diagnostics.last_binary_error = "mqtt_udp_audio_send_failed";
      g_runtime.diagnostics.last_error = g_runtime.diagnostics.last_binary_error;
    } else {
      g_runtime.sent_audio_packets += 1;
      if (g_runtime.sent_audio_packets <= 8 ||
          (g_runtime.sent_audio_packets % 50) == 0) {
        ESP_LOGI(kTag,
                 "uplink audio packets=%" PRIu32 " opus=%u datagram=%d seq=%" PRIu32 " ts=%" PRIu32,
                 g_runtime.sent_audio_packets,
                 static_cast<unsigned>(s_encoded.size()), sent, sequence,
                 timestamp_ms);
      }
    }
  }

  return true;
}

bool prime_uplink_audio_channel() {
  OpusAudioCodec *codec = nullptr;
  int encoder_frame_samples = 0;
  int encoder_output_bytes = 0;
  {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    if (!session_ready_locked() || !ensure_opus_encoder_locked()) {
      return false;
    }
    codec = &g_runtime.codec;
    encoder_frame_samples = codec->encoder_frame_samples;
    encoder_output_bytes = codec->encoder_output_bytes;
  }
  if (codec == nullptr || encoder_frame_samples <= 0 ||
      encoder_output_bytes <= 0) {
    return false;
  }

  std::vector<int16_t> silence(static_cast<size_t>(encoder_frame_samples), 0);
  std::vector<uint8_t> encoded(static_cast<size_t>(encoder_output_bytes));
  esp_audio_enc_in_frame_t input = {
      .buffer = reinterpret_cast<uint8_t *>(silence.data()),
      .len = static_cast<uint32_t>(silence.size() * sizeof(int16_t)),
  };
  esp_audio_enc_out_frame_t output = {
      .buffer = encoded.data(),
      .len = static_cast<uint32_t>(encoded.size()),
      .encoded_bytes = 0,
      .pts = 0,
  };
  if (esp_opus_enc_process(codec->encoder, &input, &output) !=
          ESP_AUDIO_ERR_OK ||
      output.encoded_bytes == 0) {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    g_runtime.diagnostics.last_binary_error = "opus_prime_encode_failed";
    g_runtime.diagnostics.last_error = g_runtime.diagnostics.last_binary_error;
    return false;
  }
  encoded.resize(output.encoded_bytes);
  const bool sent = send_service_binary(encoded);
  ESP_LOGI(kTag, "UDP audio endpoint prime sent=%d opus=%u", sent ? 1 : 0,
           static_cast<unsigned>(encoded.size()));
  return sent;
}

bool poll_local_audio_once() {
  static std::vector<int16_t> s_mic_buffer;
  bool should_listen = false;
  bool should_auto_stop = false;
  {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    if (g_runtime.conversation.control_pending) return false;
    should_listen = !g_runtime.conversation.cancelled &&
        g_runtime.session_state == AssistantSessionState::Listening;
    // WakeNet's one-microphone feed chunk is 160 samples (10 ms). Read it at
    // that cadence while idle so ESP-SR is not burst-fed six chunks at once.
    // Active conversations retain the service's 60 ms Opus frame size.
    constexpr size_t kStandbySamples = kBrowserSampleRate / 100;
    const size_t listening_samples = static_cast<size_t>(
        (kBrowserSampleRate / 1000) * kFrameDurationMs);
    s_mic_buffer.resize(should_listen ? listening_samples : kStandbySamples);
  }

  const auto bridge_status =
      lunaredu::audio::local_pcm_audio_bridge_status();
  if (!bridge_status.initialized || !bridge_status.input_enabled) {
    // A foreground lease can arrive between audio_task's gate check and
    // this function. Treat that race as a quiet cancellation.
    return false;
  }

  if (!lunaredu::pet::conversation::audio_io::read_input(
          s_mic_buffer.data(), s_mic_buffer.size(), kLocalAudioReadTimeoutMs)) {
    static uint32_t s_read_failures = 0;
    s_read_failures += 1;
    if (s_read_failures <= 8 || (s_read_failures % 100) == 0) {
      ESP_LOGW(kTag, "poll_local_audio_once read_input failed count=%" PRIu32,
               s_read_failures);
    }
    return false;
  }

  if (should_listen) {
    bool afe_output_ready = false;
    bool afe_voice_active = false;
    bool wake_frontend_ready = false;
    {
      std::lock_guard<std::mutex> lock(g_runtime.mutex);
      wake_frontend_ready = g_runtime.wake_word.afe_initialized;
    }

    lunaredu::pet::conversation::WakeWordFrontendStatus afe_status;
    if (wake_frontend_ready) {
      // Capture is half-duplex, and active speech is already normalized below.
      // Keep WakeNet out of the 60 ms uplink path; its native 10 ms feed cadence
      // is used only by the standby branch.
      std::lock_guard<std::mutex> wake_lock(g_runtime.wake_word_mutex);
      afe_status = g_runtime.wake_word_frontend.status();
    }
    {
      std::lock_guard<std::mutex> lock(g_runtime.mutex);
      refresh_wake_word_status_locked(afe_status);
    }
    if (!afe_output_ready) {
      remove_pcm_dc_bias(s_mic_buffer);
    }
    {
      std::lock_guard<std::mutex> lock(g_runtime.mutex);
      if (afe_output_ready) {
        g_runtime.vad.pcm_frames += 1;
        g_runtime.vad.last_rms = afe_voice_active ? kVadFrameEnergyThreshold : 0;
        g_runtime.vad.last_peak = afe_voice_active ? (kVadFrameEnergyThreshold * 3U) : 0;
        if (afe_voice_active) {
          g_runtime.vad.speech_frames += 1;
          g_runtime.vad.speech_ms += kFrameDurationMs;
          g_runtime.vad.silence_ms = 0;
          g_runtime.vad.current_utterance_ms += kFrameDurationMs;
          if (!g_runtime.vad.voice_activity_active &&
              g_runtime.vad.speech_ms >= kVadSpeechStartMs) {
            g_runtime.vad.voice_activity_active = true;
            g_runtime.vad.utterance_started = true;
          }
        } else {
          g_runtime.vad.silence_frames += 1;
          g_runtime.vad.silence_ms += kFrameDurationMs;
          g_runtime.vad.speech_ms = 0;
          if (g_runtime.vad.voice_activity_active &&
              g_runtime.vad.silence_ms >= kVadSpeechEndMs) {
            g_runtime.vad.voice_activity_active = false;
          }
        }
      } else {
        observe_vad_frame_locked(s_mic_buffer.data(), s_mic_buffer.size(),
                                 kFrameDurationMs);
      }
      if (should_finish_single_turn_after_silence(
              g_runtime.conversation.listening_mode.c_str(),
              g_runtime.vad.utterance_started,
              g_runtime.vad.current_utterance_ms,
              g_runtime.vad.silence_ms, kVadMinAutoStopSpeechMs,
              kVadAutoStopSilenceMs)) {
        should_auto_stop = true;
      }
    }

    if (should_auto_stop) {
      std::lock_guard<std::mutex> lock(g_runtime.mutex);
      if (!post_assistant_event_locked(
              {AssistantEventType::AutoStopRequested, {}, {}, false})) {
        g_runtime.diagnostics.last_error = "auto_stop_failed";
      }
      return false;
    }
    // Detect silence before uplink gain. Normalizing each quiet frame first
    // amplifies microphone noise into "speech", so manual turns never send
    // listen-stop and the server keeps waiting instead of answering.
    normalize_pcm_peak(s_mic_buffer, kEsp32ListeningTargetPeak,
                       kEsp32ListeningNormalizeMinPeak,
                       kEsp32ListeningMaxGainNumerator,
                       kEsp32ListeningGainDenominator);
  }

  if (!should_listen) {
    bool start_inflight = false;
    bool wake_word_enabled = false;
    std::string requested_wake_word;

    {
      std::lock_guard<std::mutex> lock(g_runtime.mutex);
      start_inflight = g_runtime.conversation.control_pending || session_start_inflight_locked();
      wake_word_enabled = g_runtime.wake_word.enabled;
      requested_wake_word = g_runtime.wake_word.requested_word;
    }
    if (start_inflight) {
      return true;
    }

    static std::vector<int16_t> s_ref_buffer;
    s_ref_buffer.assign(s_mic_buffer.size(), 0);
    lunaredu::pet::conversation::audio_io::read_output_reference(
        s_ref_buffer.data(), s_ref_buffer.size());
    uint32_t ref_peak = 0;
    for (const int16_t sample : s_ref_buffer) {
      ref_peak = std::max(ref_peak, static_cast<uint32_t>(std::abs(sample)));
    }
    const bool is_speaker_playing = ref_peak > 100;

    const auto statistics = standby_pcm_statistics(s_mic_buffer.data(), s_mic_buffer.size());
    const uint32_t peak = statistics.peak;
    const uint32_t rms = statistics.rms;
    const uint32_t raw_peak = statistics.raw_peak;
    const uint32_t raw_rms = statistics.raw_rms;
    const int32_t raw_mean = statistics.raw_mean;
    const bool speech_frame =
        !is_speaker_playing &&
        (rms >= kVadFrameEnergyThreshold ||
         peak >= kVadFrameEnergyThreshold * 3U);

    lunaredu::pet::conversation::WakeWordFrontendEvent event;
    lunaredu::pet::conversation::WakeWordFrontendStatus frontend_status;
    bool processed = false;
    {
      std::lock_guard<std::mutex> wake_lock(g_runtime.wake_word_mutex);
      frontend_status = g_runtime.wake_word_frontend.status();
      if (frontend_status.initialized) {
        g_runtime.wake_word_frontend.feed_output_reference(
            s_ref_buffer.data(), s_ref_buffer.size());
        processed = g_runtime.wake_word_frontend.process(
            s_mic_buffer, false, wake_word_enabled, requested_wake_word,
            event);
        frontend_status = g_runtime.wake_word_frontend.status();
      }
    }

    if (!frontend_status.initialized) {
      std::lock_guard<std::mutex> lock(g_runtime.mutex);
      refresh_wake_word_status_locked(frontend_status);
      if (g_runtime.wake_word.afe_last_error.empty()) {
        g_runtime.wake_word.afe_last_error = "wake_word_frontend_not_ready";
      }
      return false;
    }

    bool wake_word_ignored = false;
    bool wake_word_unavailable = false;
    bool start_wake_word_session = false;
    const bool microphone_enabled =
        lunaredu::pet::conversation::assistant_microphone_enabled();
    std::string unavailable_word;
    const uint32_t processed_frame_duration_ms = std::max<uint32_t>(
        1, (frontend_status.fetch_chunk_samples * 1000U) /
               static_cast<uint32_t>(kBrowserSampleRate));
    {
      // ESP-SR feed/fetch can consume most of one S3 core. Never hold the
      // assistant state lock while it runs: UI snapshots, touch intents and
      // network callbacks all need this lock to remain responsive.
      std::lock_guard<std::mutex> lock(g_runtime.mutex);
      g_runtime.vad.last_raw_rms = raw_rms;
      g_runtime.vad.last_raw_peak = raw_peak;
      g_runtime.vad.last_raw_mean = raw_mean;
      g_runtime.vad.last_rms = rms;
      g_runtime.vad.last_peak = peak;
      refresh_wake_word_status_locked(frontend_status);
      if (g_runtime.wake_word.enabled &&
          !frontend_status.requested_wake_word_available) {
        g_runtime.wake_word.afe_last_error =
            "requested_wake_word_unavailable";
      }

      if (processed) {
        if (event.voice_activity_active && speech_frame) {
          g_runtime.vad.speech_ms += processed_frame_duration_ms;
          g_runtime.vad.silence_ms = 0;
          if (!g_runtime.vad.voice_activity_active &&
              g_runtime.vad.speech_ms >= kVadSpeechStartMs) {
            g_runtime.vad.voice_activity_active = true;
            if (g_runtime.conversation.standby_mode ==
                StandbyListenMode::ContinuousConversation) {
              (void)post_assistant_event_locked(
                  {AssistantEventType::ContinuousStartRequested, {}, {},
                   false});
            }
          }
        } else {
          g_runtime.vad.silence_ms += processed_frame_duration_ms;
          g_runtime.vad.speech_ms = 0;
          if (g_runtime.vad.voice_activity_active &&
              g_runtime.vad.silence_ms >= kVadSpeechEndMs) {
            g_runtime.vad.voice_activity_active = false;
          }
        }
      }

      if (event.wake_word_detected && !microphone_enabled) {
        // Disabling the microphone stops subsequent I2S reads, but an AFE
        // result produced from the final buffered frame can still arrive.
        // Treat the persisted user preference as the final authority here.
        wake_word_ignored = true;
      } else if (event.wake_word_detected) {
        const int64_t now = now_ms();
        if (g_runtime.conversation.last_wake_word_trigger_at_ms > 0 &&
            (now - g_runtime.conversation.last_wake_word_trigger_at_ms) <
                static_cast<int64_t>(kWakeWordTriggerCooldownMs)) {
          wake_word_ignored = true;
        } else {
          g_runtime.conversation.last_wake_word_trigger_at_ms = now;
          g_runtime.conversation.wake_word_resume_not_before_ms =
              now + static_cast<int64_t>(kWakeWordTriggerCooldownMs);
          if (g_runtime.wake_word.enabled &&
              !frontend_status.requested_wake_word_available) {
            g_runtime.diagnostics.last_error =
                "requested_wake_word_unavailable";
            unavailable_word = g_runtime.wake_word.requested_word;
            wake_word_unavailable = true;
          } else {
            g_runtime.diagnostics.last_message_summary = event.wake_word;
            start_wake_word_session = true;
          }
        }
      }
    }

    if (wake_word_ignored) {
      return true;
    }
    if (wake_word_unavailable) {
      conversation_emit_status("唤醒词异常", "当前唤醒词资源不可用",
                             unavailable_word.c_str(), nullptr);
      return true;
    }
    if (start_wake_word_session) {
      conversation_emit_status("已唤醒", "正在切换到语音模式",
                             event.wake_word.c_str(), nullptr);
      if (!conversation_request_audio_focus("wake_word", 500)) {
        ESP_LOGW(kTag, "audio focus request failed before wake chime");
        conversation_emit_status("音频忙", "音乐未能安全暂停，已取消语音启动",
                                  event.wake_word.c_str(),
                                  "assistant_audio_focus_unavailable");
        return true;
      }
      lunaredu::pet::conversation::audio_io::enable_input(false);
      lunaredu::pet::conversation::audio_io::clear_output_reference();
      {
        std::lock_guard<std::mutex> wake_lock(g_runtime.wake_word_mutex);
        g_runtime.wake_word_frontend.reset();
      }
      local_audio_app_play_wake_chime();
      bool queued = false;
      {
        std::lock_guard<std::mutex> lock(g_runtime.mutex);
        queued = post_assistant_event_locked(
            {AssistantEventType::WakeWordStartRequested, event.wake_word, "",
             false});
      }
      if (!queued) conversation_release_audio_focus("wake_word_event_queue_full");
    }
    return true;
  }

  {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    if (g_runtime.conversation.cancelled || g_runtime.conversation.control_pending ||
        g_runtime.session_state != AssistantSessionState::Listening ||
        g_runtime.close_audio_channel_requested.load() ||
        !session_ready_locked()) {
      return false;
    }
  }
  process_uplink_audio_work_item(s_mic_buffer);
  return true;
}

namespace {

bool process_next_downlink_audio(TickType_t wait_ticks) {
  if (g_runtime.downlink_audio_queue == nullptr) {
    if (wait_ticks > 0) {
      vTaskDelay(wait_ticks);
    }
    return false;
  }

  DownlinkAudioWorkItem *item = nullptr;
  if (xQueueReceive(g_runtime.downlink_audio_queue, &item, wait_ticks) != pdTRUE ||
      item == nullptr) {
    return false;
  }

  std::unique_ptr<DownlinkAudioWorkItem, decltype(&DownlinkAudioWorkItem::release)>
      holder(item, DownlinkAudioWorkItem::release);
  const auto audio_policy = lunaredu::pet::conversation::assistant_policy_snapshot();
  {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    // Foreground focus is published before set_suspension() acquires the
    // runtime lock. Reject a stale packet on either signal so this worker
    // cannot disable a microphone that has just changed owners.
    if (!g_runtime.service_enabled || g_runtime.service_suspended ||
        !audio_policy.audio_focus_owner.empty() ||
        holder->generation != g_runtime.conversation.generation ||
        !g_runtime.conversation.accepts_response(g_runtime.session_state)) {
      return true;
    }
    g_runtime.conversation.downlink_audio_active = true;
  }
  if (lunaredu::audio::local_pcm_audio_bridge_status().input_enabled) {
    ESP_LOGI(kTag, "audio_task enable_input=false reason=downlink");
    (void)lunaredu::pet::conversation::audio_io::enable_input(false);
  }
  handle_server_binary(holder->frame, holder->generation, holder->sequence);
  {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    g_runtime.conversation.downlink_audio_active = false;
    maybe_post_audio_playback_finished_locked("audio_task");
  }
  return true;
}

} // namespace

void quiesce_audio() {
  std::lock_guard<std::mutex> lock(g_audio_iteration_mutex);
  drain_downlink_audio_queue();
  lunaredu::audio::local_pcm_audio_bridge_enable_input(false);
  lunaredu::audio::local_pcm_audio_bridge_enable_output(false);
}

void audio_task(void *argument) {
  (void)argument;
  ESP_LOGI(kTag, "audio_task started");

  for (;;) {
    std::lock_guard<std::mutex> iteration(g_audio_iteration_mutex);
    // Playback has priority over beginning another microphone read. A read
    // already in progress remains bounded by kLocalAudioReadTimeoutMs.
    if (process_next_downlink_audio(0)) {
      continue;
    }

    bool prime_uplink_requested = false;
    bool should_capture = false;
    bool listening = false;
    bool start_inflight = false;
#if CONFIG_LOG_MAXIMUM_LEVEL >= 4
    bool standby_allowed = false;
#endif
    bool capture_delay_elapsed = true;
    bool wake_word_resume_allowed = true;
    bool awaiting_response = false;
    bool network_connected = false;
    bool activation_ready = false;
    bool wake_frontend_ready = false;
    bool standby_wake_word_capture = false;
    bool continuous_capture = false;
    bool input_enabled = false;
    bool service_enabled = false;
    AssistantSessionState session_state = AssistantSessionState::Inactive;
    {
      std::lock_guard<std::mutex> lock(g_runtime.mutex);
      service_enabled =
          g_runtime.service_enabled && !g_runtime.service_suspended &&
          !g_runtime.conversation.control_pending;
      prime_uplink_requested =
          g_runtime.conversation.prime_uplink_requested;
      g_runtime.conversation.prime_uplink_requested = false;
      session_state = g_runtime.session_state;
      listening = !g_runtime.conversation.cancelled &&
          session_state == AssistantSessionState::Listening;
      start_inflight = session_start_inflight_locked();
      network_connected = assistant_network_connected();
      activation_ready = g_runtime.activation.ready;
      wake_frontend_ready = g_runtime.wake_word.afe_initialized;
      awaiting_response = g_runtime.conversation.awaiting_response;
#if CONFIG_LOG_MAXIMUM_LEVEL >= 4
      standby_allowed =
          wake_frontend_ready &&
          ((g_runtime.conversation.standby_mode == StandbyListenMode::WakeWord &&
            g_runtime.wake_word.enabled &&
            !g_runtime.wake_word.detection_in_listening) ||
           (network_connected && activation_ready &&
            g_runtime.conversation.standby_mode ==
                StandbyListenMode::ContinuousConversation));
#endif
      capture_delay_elapsed =
          g_runtime.conversation.local_audio_capture_not_before_ms <= 0 ||
          now_ms() >= g_runtime.conversation.local_audio_capture_not_before_ms;
      wake_word_resume_allowed =
          g_runtime.conversation.wake_word_resume_not_before_ms <= 0 ||
          now_ms() >= g_runtime.conversation.wake_word_resume_not_before_ms;
      const lunaredu::pet::conversation::StandbyCapturePolicyInput capture_policy{
          .service_available = service_enabled,
          .network_connected = network_connected,
          .activation_ready = activation_ready,
          .wake_frontend_ready = wake_frontend_ready,
          .wake_word_mode = g_runtime.conversation.standby_mode ==
                            StandbyListenMode::WakeWord,
          .wake_word_enabled = g_runtime.wake_word.enabled,
          .wake_word_detection_in_listening =
              g_runtime.wake_word.detection_in_listening,
          .continuous_mode = g_runtime.conversation.standby_mode ==
                             StandbyListenMode::ContinuousConversation,
          .session_idle = session_idle_available_locked(),
          .listening = listening,
          .speaking = session_state == AssistantSessionState::Speaking,
          .session_start_inflight = start_inflight,
          .awaiting_response = awaiting_response,
          .wake_word_resume_allowed = wake_word_resume_allowed,
      };
      standby_wake_word_capture =
          lunaredu::pet::conversation::standby_wake_word_capture_allowed(capture_policy);
      continuous_capture =
          lunaredu::pet::conversation::continuous_standby_capture_allowed(capture_policy);
      should_capture = lunaredu::pet::conversation::assistant_microphone_enabled() &&
                       service_enabled &&
                       ((listening && capture_delay_elapsed) ||
                        standby_wake_word_capture || continuous_capture);
    }
    bool service_suspended = false;
    {
      std::lock_guard<std::mutex> lock(g_runtime.mutex);
      service_suspended = g_runtime.service_suspended;
    }
    if (service_suspended) {
      // Suspension gates capture, but does not tear down ESP-SR.  Ordinary
      // foreground leases and cloud maintenance are frequent; repeatedly
      // destroying/recreating the AFE here fragments internal SRAM.  The
      // explicit cloud-maintenance path requests a release only when it has
      // already passed its TLS memory admission check.
      (void)process_next_downlink_audio(pdMS_TO_TICKS(10));
      continue;
    }
    if (prime_uplink_requested) {
      // Keep the Opus encoder on the unified 28 KiB audio stack. Running
      // this from pet_network overflowed its 8 KiB internal stack and caused
      // the visible "service reconnecting" reboot on the first response.
      const bool primed = prime_uplink_audio_channel();
      (void)post_assistant_event(
          {AssistantEventType::AudioUplinkPrimed, {}, {}, primed});
    }
    input_enabled =
        lunaredu::audio::local_pcm_audio_bridge_status().input_enabled;

#if CONFIG_LOG_MAXIMUM_LEVEL >= 4
    static int64_t s_last_gate_log_at_ms = 0;
    const int64_t current_gate_ms = now_ms();
    if (!should_capture && current_gate_ms - s_last_gate_log_at_ms >= 5000) {
      s_last_gate_log_at_ms = current_gate_ms;
      ESP_LOGD(kTag,
               "local_audio gate capture=0 state=%s network=%d activation=%d afe=%d standby=%d wake_capture=%d continuous=%d awaiting_response=%d start_pending=%d resume_allowed=%d delay_elapsed=%d input_enabled=%d",
               assistant_session_state_name(session_state), network_connected,
               activation_ready, wake_frontend_ready, standby_allowed,
               standby_wake_word_capture, continuous_capture, awaiting_response,
               start_inflight, wake_word_resume_allowed, capture_delay_elapsed,
               input_enabled);
    }
#endif

    if (should_capture && !input_enabled) {
      ESP_LOGI(kTag,
               "local_audio enable_input=true listening=%d start_pending=%d capture_delay_elapsed=%d",
               listening, start_inflight, capture_delay_elapsed);
      if (lunaredu::pet::conversation::audio_io::enable_input(true)) {
      } else {
        std::lock_guard<std::mutex> lock(g_runtime.mutex);
        g_runtime.diagnostics.last_error = "enable_local_audio_input_failed";
        conversation_emit_status("音频异常", "板载麦克风启用失败", "",
                                  g_runtime.diagnostics.last_error.c_str());
      }
    } else if (!should_capture && input_enabled) {
      ESP_LOGI(kTag,
               "local_audio enable_input=false listening=%d should_capture=%d",
               listening, should_capture);
      lunaredu::pet::conversation::audio_io::enable_input(false);
    }

    if (input_enabled && should_capture) {
      poll_local_audio_once();
#if CONFIG_LOG_MAXIMUM_LEVEL >= 4
      static int64_t s_last_audio_stats_at_ms = 0;
      const int64_t current_ms = now_ms();
      if (current_ms - s_last_audio_stats_at_ms >= 2000) {
        s_last_audio_stats_at_ms = current_ms;
        const auto audio_status =
            lunaredu::audio::local_pcm_audio_bridge_status();
        lunaredu::pet::conversation::WakeWordFrontendStatus afe_status;
        uint32_t vad_rms = 0;
        uint32_t vad_peak = 0;
        uint32_t raw_rms = 0;
        uint32_t raw_peak = 0;
        int32_t raw_mean = 0;
        {
          std::lock_guard<std::mutex> lock(g_runtime.mutex);
          vad_rms = g_runtime.vad.last_rms;
          vad_peak = g_runtime.vad.last_peak;
          raw_rms = g_runtime.vad.last_raw_rms;
          raw_peak = g_runtime.vad.last_raw_peak;
          raw_mean = g_runtime.vad.last_raw_mean;
        }
        {
          std::lock_guard<std::mutex> lock(g_runtime.wake_word_mutex);
          afe_status = g_runtime.wake_word_frontend.status();
        }
        ESP_LOGD(kTag,
                 "local_audio stats capture=%d listening=%d input_frames=%" PRIu32 " input_failures=%" PRIu32 " afe_input=%" PRIu32 " afe_feed=%" PRIu32 " last_fed=%" PRIu32 " warmup_fed=%" PRIu32 " buffered=%" PRIu32 " fetch_failures=%" PRIu32 " empty_fetch_skips=%" PRIu32 " standby_fetch=%" PRIu32 " vad_changes=%" PRIu32 " wake_hits=%" PRIu32 " raw_rms=%" PRIu32 " raw_peak=%" PRIu32 " raw_mean=%" PRId32 " vad_rms=%" PRIu32 " vad_peak=%" PRIu32 " last_samples=%" PRIu32 " feed_chunk=%" PRIu32 " feed_channels=%" PRIu32 " fetch_chunk=%" PRIu32 " audio_error=%s afe_error=%s",
                 should_capture, listening, audio_status.input_frames,
                 audio_status.input_failures, afe_status.input_frames,
                 afe_status.feed_chunks, afe_status.last_fed_chunks,
                 afe_status.fed_chunks_since_reset,
                 afe_status.buffered_input_samples, afe_status.fetch_failures,
                 afe_status.empty_fetch_skips, afe_status.standby_fetch_events,
                 afe_status.vad_state_changes, afe_status.wake_word_detections,
                 raw_rms, raw_peak, raw_mean, vad_rms, vad_peak,
                 afe_status.last_input_samples, afe_status.feed_chunk_samples,
                 afe_status.feed_channel_count, afe_status.fetch_chunk_samples,
                 audio_status.last_error.empty() ? "-"
                                                 : audio_status.last_error.c_str(),
                 afe_status.last_error.empty() ? "-"
                                               : afe_status.last_error.c_str());
      }
#endif
      vTaskDelay(pdMS_TO_TICKS(1));
    } else {
      (void)process_next_downlink_audio(pdMS_TO_TICKS(10));
    }
  }
}

} // namespace lunaredu::pet::conversation::internal
