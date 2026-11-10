#include "interaction/voice/resident_audio.h"
#include "interaction/tools/assistant_mcp.h"
#include "interaction/conversation/assistant_session_internal.h"
#include "interaction/conversation/assistant_core.h"
#include "interaction/conversation/assistant_loop.h"
#include "lunaredu_audio_bridge.h"
#include "interaction/transport/assistant_transport.h"
#include "interaction/conversation/host_events.h"

#include <algorithm>
#include <cinttypes>
#include <cstdlib>

#include <esp_opus_dec.h>

namespace lunaredu::pet::conversation::internal {

namespace {

uint32_t pcm_peak_abs(const int16_t *samples, size_t sample_count) {
  uint32_t peak = 0;
  if (samples == nullptr) {
    return peak;
  }
  for (size_t index = 0; index < sample_count; ++index) {
    peak = std::max(peak, static_cast<uint32_t>(std::abs(samples[index])));
  }
  return peak;
}

int16_t lerp_pcm_i16(int16_t a, int16_t b, uint32_t fraction,
                     uint32_t denominator) {
  const int32_t delta = static_cast<int32_t>(b) - static_cast<int32_t>(a);
  const int32_t scaled =
      static_cast<int32_t>(a) +
      static_cast<int32_t>((static_cast<int64_t>(delta) * fraction) /
                           denominator);
  return clamp_pcm_i32(scaled);
}

void resample_pcm_linear(const int16_t *input, size_t input_count,
                         uint32_t input_rate, uint32_t output_rate,
                         std::vector<int16_t> &output) {
  output.clear();
  if (input == nullptr || input_count == 0 || input_rate == 0 ||
      output_rate == 0) {
    return;
  }
  if (input_rate == output_rate) {
    output.assign(input, input + input_count);
    return;
  }

  const size_t output_count = std::max<size_t>(
      1, (static_cast<uint64_t>(input_count) * output_rate + input_rate / 2) /
             input_rate);
  output.resize(output_count);
  for (size_t index = 0; index < output_count; ++index) {
    const uint64_t position =
        static_cast<uint64_t>(index) * static_cast<uint64_t>(input_rate);
    const size_t base =
        std::min<size_t>(position / output_rate, input_count - 1);
    const size_t next = std::min<size_t>(base + 1, input_count - 1);
    const uint32_t fraction = static_cast<uint32_t>(position % output_rate);
    output[index] =
        lerp_pcm_i16(input[base], input[next], fraction, output_rate);
  }
}

const char *udp_decode_status_name(XiaozhiMqttUdpDecodeStatus status) {
  switch (status) {
  case XiaozhiMqttUdpDecodeStatus::Ok:
    return "ok";
  case XiaozhiMqttUdpDecodeStatus::SequenceGap:
    return "sequence_gap";
  case XiaozhiMqttUdpDecodeStatus::StaleSequence:
    return "stale_sequence";
  case XiaozhiMqttUdpDecodeStatus::InvalidEnvelope:
    return "invalid_envelope";
  case XiaozhiMqttUdpDecodeStatus::CryptoError:
    return "crypto_error";
  case XiaozhiMqttUdpDecodeStatus::NotConfigured:
    return "not_configured";
  default:
    return "unknown";
  }
}

} // namespace

void handle_server_text_locked(const std::string &message, std::unique_lock<std::mutex> &runtime_lock) {
  g_runtime.transport.last_service_activity_at_ms = now_ms();
  g_runtime.rx_doc.clear();
  if (deserializeJson(g_runtime.rx_doc, message) != DeserializationError::Ok) {
    g_runtime.diagnostics.last_error = "invalid_server_text_json";
    return;
  }

  const std::string type = g_runtime.rx_doc["type"] | "";
  if (type == "hello") {
      XiaozhiUdpHelloConfig udp_config;
      if (!xiaozhi_parse_mqtt_server_hello(message, udp_config) || !udp_config.ok) {
        g_runtime.diagnostics.last_error =
            udp_config.error.empty() ? "invalid_mqtt_server_hello"
                                     : udp_config.error;
        return;
      }
      XiaozhiMqttUdpConfig channel_config;
      channel_config.server = udp_config.server;
      channel_config.port = udp_config.port;
      channel_config.key_hex = udp_config.key;
      channel_config.nonce_hex = udp_config.nonce;
      if (!g_runtime.transport.mqtt_udp_channel.configure(channel_config)) {
        g_runtime.diagnostics.last_error = "mqtt_udp_channel_config_failed";
        return;
      }
      ESP_LOGI(kTag,
               "service hello udp server=%s:%d session=%s sample_rate=%d frame_ms=%d",
               udp_config.server.c_str(), udp_config.port,
               udp_config.session_id.c_str(), udp_config.sample_rate,
               udp_config.frame_duration_ms);
      if (g_runtime.transport.udp_channel == nullptr) {
        g_runtime.transport.udp_channel = network_open_udp_channel();
        if (g_runtime.transport.udp_channel == nullptr) {
          g_runtime.diagnostics.last_error = "failed_to_init_service_udp";
          return;
        }
        g_runtime.transport.udp_channel->OnMessage([](const std::string &data) {
          std::vector<uint8_t> payload;
          XiaozhiMqttUdpDecodeStatus decode_status =
              XiaozhiMqttUdpDecodeStatus::InvalidEnvelope;
          uint32_t sequence = 0;
          uint32_t expected_sequence = 0;
          uint32_t timestamp_ms = 0;
          {
            std::lock_guard<std::mutex> lock(g_runtime.mutex);
            auto result = g_runtime.transport.mqtt_udp_channel.decrypt_payload(
                reinterpret_cast<const uint8_t *>(data.data()), data.size());
            decode_status = result.status;
            sequence = result.sequence;
            expected_sequence = result.expected_sequence;
            timestamp_ms = result.timestamp_ms;
            payload = std::move(result.payload);
            if (decode_status == XiaozhiMqttUdpDecodeStatus::SequenceGap) {
              g_runtime.downlink_sequence_gaps += 1;
            } else if (decode_status == XiaozhiMqttUdpDecodeStatus::StaleSequence) {
              g_runtime.downlink_stale_packets += 1;
            }
            if (decode_status == XiaozhiMqttUdpDecodeStatus::InvalidEnvelope ||
                decode_status == XiaozhiMqttUdpDecodeStatus::CryptoError ||
                decode_status == XiaozhiMqttUdpDecodeStatus::NotConfigured) {
              g_runtime.diagnostics.last_binary_error =
                  "mqtt_udp_audio_decode_failed";
              g_runtime.diagnostics.last_error =
                  g_runtime.diagnostics.last_binary_error;
              g_runtime.downlink_decode_failures += 1;
              return;
            }
          }
          static uint32_t s_udp_decode_logs = 0;
          if (s_udp_decode_logs < 8 ||
              decode_status != XiaozhiMqttUdpDecodeStatus::Ok) {
            s_udp_decode_logs += 1;
            ESP_LOGI(kTag,
                     "service udp datagram bytes=%u status=%s payload=%u seq=%" PRIu32 " expected=%" PRIu32 " ts=%" PRIu32,
                     static_cast<unsigned>(data.size()),
                     udp_decode_status_name(decode_status),
                     static_cast<unsigned>(payload.size()), sequence,
                     expected_sequence, timestamp_ms);
          }
          if (payload.empty()) {
            std::lock_guard<std::mutex> lock(g_runtime.mutex);
            g_runtime.diagnostics.last_binary_error =
                "mqtt_udp_audio_empty_payload";
            g_runtime.diagnostics.last_error =
                g_runtime.diagnostics.last_binary_error;
            g_runtime.downlink_decode_failures += 1;
            return;
          }
          (void)enqueue_downlink_audio(std::move(payload));
        });
      }
      g_runtime.transport.session_id = udp_config.session_id;
      g_runtime.transport.server_output_sample_rate = udp_config.sample_rate;
      g_runtime.transport.server_output_frame_duration_ms =
          udp_config.frame_duration_ms;
      (void)post_assistant_event_locked(
          {AssistantEventType::SessionHelloReceived, {}, {}, false});
      return;
  }

  g_runtime.diagnostics.last_message_summary = xiaozhi_summarize_message(message);

  if (type == "tts") {
    g_runtime.conversation.last_tts_state = g_runtime.rx_doc["state"] | "";
    if (g_runtime.conversation.last_tts_state == "sentence_start") {
      const std::string text = g_runtime.rx_doc["text"] | "";
      if (!text.empty()) {
        g_runtime.conversation.transcript_text = text;
        g_runtime.conversation.transcript_role = "assistant";
        g_runtime.conversation.transcript_revision += 1;
        conversation_emit_transcript(text);
      }
    }
    const UBaseType_t queued =
        g_runtime.downlink_audio_queue == nullptr
            ? 0
            : uxQueueMessagesWaiting(g_runtime.downlink_audio_queue);
    ESP_LOGI(kTag,
             "service tts event state=%s mode=%s standby=%s session=%s app_state=%s incoming_audio=%" PRIu32 " queued=%u playback_active=%d",
             g_runtime.conversation.last_tts_state.c_str(),
             g_runtime.conversation.listening_mode.c_str(),
             standby_listen_mode_name(g_runtime.conversation.standby_mode),
             g_runtime.transport.session_id.empty()
                 ? "-"
                 : g_runtime.transport.session_id.c_str(),
             assistant_session_state_name(g_runtime.session_state),
             g_runtime.incoming_audio_packets, static_cast<unsigned>(queued),
             g_runtime.conversation.downlink_audio_active);
    if (g_runtime.conversation.last_tts_state == "start") {
      g_runtime.conversation.awaiting_response = false;
      g_runtime.conversation.tts_stop_waiting_for_playback = false;
      g_runtime.conversation.tts_stop_playback_grace_deadline_ms = 0;
      (void)post_assistant_event_locked(
          {AssistantEventType::TtsStarted, {}, {}, false});
    } else if (g_runtime.conversation.last_tts_state == "stop") {
      g_runtime.conversation.tts_stop_waiting_for_playback = true;
      g_runtime.conversation.tts_stop_playback_grace_deadline_ms =
          now_ms() + static_cast<int64_t>(kTtsStopPlaybackDrainGraceMs);
      (void)post_assistant_event_locked(
          {AssistantEventType::TtsStopped, {}, {}, false});
    }
  } else if (type == "mcp") {
    JsonVariant payload = g_runtime.rx_doc["payload"];
    handle_mcp_message_locked({g_runtime.pending_mcp_messages,
        g_runtime.external_tools, g_runtime.transport.session_id,
        g_runtime.diagnostics.last_error}, payload, runtime_lock);
  } else if (type == "goodbye") {
    const std::string goodbye_session_id = g_runtime.rx_doc["session_id"] | "";
    if (goodbye_session_id.empty() ||
        goodbye_session_id == g_runtime.transport.session_id) {
      g_runtime.close_audio_channel_requested.store(true);
      (void)post_assistant_event_locked(
          {AssistantEventType::SessionGoodbye, {}, {}, false});
    }
  } else if (type == "stt") {
    if (session_listening_locked() || !g_runtime.rx_doc["text"].isNull()) {
      // Extract STT text for display
      const std::string stt_text = g_runtime.rx_doc["text"] | "";
      const std::string display_text = stt_text.empty()
          ? g_runtime.diagnostics.last_message_summary
          : stt_text;
      if (!display_text.empty()) {
        g_runtime.conversation.transcript_text = display_text;
        g_runtime.conversation.transcript_role = "user";
        g_runtime.conversation.transcript_revision += 1;
      }
      conversation_emit_transcript(display_text);
      (void)post_assistant_event_locked(
          {AssistantEventType::SttActivityReceived,
           display_text, {}, false});
    }
  } else if (type == "listen") {
    const std::string listen_state = g_runtime.rx_doc["state"] | "";
    ESP_LOGI(kTag,
             "service listen event state=%s mode=%s session=%s app_state=%s",
             listen_state.c_str(),
             g_runtime.conversation.listening_mode.c_str(),
             g_runtime.transport.session_id.empty()
                 ? "-"
                 : g_runtime.transport.session_id.c_str(),
             assistant_session_state_name(g_runtime.session_state));
    if (listen_state == "stop") {
      if (session_listening_locked()) {
        (void)post_assistant_event_locked(
            {AssistantEventType::AutoStopRequested, {}, {}, false});
      } else {
        ESP_LOGI(kTag,
                 "service listen stop ignored state=%s mode=%s session=%s",
                 assistant_session_state_name(g_runtime.session_state),
                 g_runtime.conversation.listening_mode.c_str(),
                 g_runtime.transport.session_id.empty()
                     ? "-"
                     : g_runtime.transport.session_id.c_str());
      }
    }
  } else if (type == "llm") {
    const std::string emotion = g_runtime.rx_doc["emotion"] | "";
    if (!emotion.empty()) {
      (void)post_assistant_event_locked(
          {AssistantEventType::EmotionReceived, emotion, {}, false});
    }
  }
}

void handle_server_binary(const std::vector<uint8_t> &frame) {
  uint32_t sample_rate;
  uint32_t frame_duration_ms;
  void* decoder = nullptr;
  {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    // A downlink packet can already be in the decoder when a foreground
    // application acquires the audio lease.  Never let that stale packet
    // reopen or write the shared audio channel after suspension.
    if (!g_runtime.service_enabled || g_runtime.service_suspended) {
      return;
    }
    g_runtime.transport.last_service_activity_at_ms = now_ms();
    sample_rate = g_runtime.transport.server_output_sample_rate;
    frame_duration_ms = g_runtime.transport.server_output_frame_duration_ms;
    if (!ensure_opus_decoder_locked(sample_rate, frame_duration_ms)) {
      return;
    }
    decoder = g_runtime.codec.decoder;
  }

  if (frame.empty()) {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    g_runtime.diagnostics.last_binary_error = "audio_packet_decode_failed";
    g_runtime.diagnostics.last_error = g_runtime.diagnostics.last_binary_error;
    g_runtime.downlink_decode_failures += 1;
    return;
  }
  const std::vector<uint8_t> &opus_payload = frame;

  static std::vector<int16_t> s_pcm_samples;
  const size_t expected_samples = static_cast<size_t>((sample_rate / 1000) * frame_duration_ms);
  s_pcm_samples.assign(expected_samples, 0);
  esp_audio_dec_in_raw_t input = {
      .buffer = const_cast<uint8_t *>(opus_payload.data()),
      .len = static_cast<uint32_t>(opus_payload.size()),
      .consumed = 0,
      .frame_recover = ESP_AUDIO_DEC_RECOVERY_NONE,
  };
  esp_audio_dec_out_frame_t output = {
      .buffer = reinterpret_cast<uint8_t *>(s_pcm_samples.data()),
      .len = static_cast<uint32_t>(s_pcm_samples.size() * sizeof(int16_t)),
      .needed_size = 0,
      .decoded_size = 0,
  };
  esp_audio_dec_info_t info = {};
  const int result = esp_opus_dec_decode(decoder, &input, &output, &info);
  if (result != ESP_AUDIO_ERR_OK || output.decoded_size == 0) {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    g_runtime.diagnostics.last_binary_error = "audio_decode_failed";
    g_runtime.diagnostics.last_error = g_runtime.diagnostics.last_binary_error;
    g_runtime.downlink_decode_failures += 1;
    return;
  }

  const size_t sample_count = output.decoded_size / sizeof(int16_t);
  const uint32_t peak = pcm_peak_abs(s_pcm_samples.data(), sample_count);

  {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    if (!g_runtime.service_enabled || g_runtime.service_suspended) {
      return;
    }
  }
  
  const auto local_audio = lunaredu::audio::local_pcm_audio_bridge_status();
  const uint32_t output_sample_rate =
      local_audio.output_sample_rate > 0
          ? static_cast<uint32_t>(local_audio.output_sample_rate)
          : sample_rate;
  static std::vector<int16_t> s_playback_samples;
  const int16_t *playback_samples = s_pcm_samples.data();
  size_t playback_sample_count = sample_count;
  if (output_sample_rate != sample_rate) {
    resample_pcm_linear(s_pcm_samples.data(), sample_count, sample_rate,
                        output_sample_rate, s_playback_samples);
    if (!s_playback_samples.empty()) {
      playback_samples = s_playback_samples.data();
      playback_sample_count = s_playback_samples.size();
    }
  }

  if (!local_audio.output_enabled &&
      !lunaredu::pet::conversation::audio_io::enable_output(true)) {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    g_runtime.diagnostics.last_playback_error = "enable_local_audio_output_failed";
    g_runtime.diagnostics.last_error = g_runtime.diagnostics.last_playback_error;
    g_runtime.downlink_playback_failures += 1;
    return;
  }
  const uint32_t output_timeout_ms =
      std::max<uint32_t>(120U, frame_duration_ms + 80U);
  if (!lunaredu::pet::conversation::audio_io::write_output(
          playback_samples, playback_sample_count, output_timeout_ms)) {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    g_runtime.diagnostics.last_playback_error = "local_audio_write_failed";
    g_runtime.diagnostics.last_error = g_runtime.diagnostics.last_playback_error;
    g_runtime.downlink_playback_failures += 1;
    return;
  }
  // Count a packet as received for playback-drain purposes only after the
  // decoded PCM has actually reached the I2S bridge.  Counting before output
  // enable/write made a silent speaker look like a successful TTS response.
  {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    g_runtime.incoming_audio_packets += 1;
    g_runtime.diagnostics.last_binary_error.clear();
  }
  const auto output_status = lunaredu::audio::local_pcm_audio_bridge_status();
  uint32_t incoming_audio_packets = 0;
  {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    incoming_audio_packets = g_runtime.incoming_audio_packets;
  }
  if (incoming_audio_packets <= 5 || (incoming_audio_packets % 25) == 0) {
    ESP_LOGI(kTag,
             "playback frame packets=%" PRIu32 " encoded=%u opus=%u decoded_samples=%u playback_samples=%u peak=%" PRIu32 " sample_rate=%" PRIu32 " output_rate=%" PRIu32 " frame_ms=%" PRIu32 " output_frames=%" PRIu32 " output_failures=%" PRIu32 " timeout_ms=%" PRIu32 " output_error=%s",
             incoming_audio_packets,
             static_cast<unsigned>(frame.size()),
             static_cast<unsigned>(opus_payload.size()),
             static_cast<unsigned>(sample_count),
             static_cast<unsigned>(playback_sample_count), peak, sample_rate,
             output_sample_rate, frame_duration_ms, output_status.output_frames,
             output_status.output_failures, output_timeout_ms,
             output_status.last_error.empty() ? "-"
                                              : output_status.last_error.c_str());
  }
}

bool enqueue_downlink_audio(std::vector<uint8_t> frame) {
  QueueHandle_t queue = nullptr;
  {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    if (!g_runtime.service_enabled || g_runtime.service_suspended) {
      return false;
    }
    queue = g_runtime.downlink_audio_queue;
    if (queue == nullptr) {
      g_runtime.diagnostics.last_binary_error = "downlink_audio_queue_unavailable";
      g_runtime.diagnostics.last_error = g_runtime.diagnostics.last_binary_error;
      g_runtime.downlink_queue_drops += 1;
      return false;
    }
  }
  DownlinkAudioWorkItem *raw =
      g_runtime.downlink_audio_pool.acquire(std::move(frame));
  const BaseType_t queued = raw == nullptr ? pdFALSE :
      xQueueSend(queue, &raw, pdMS_TO_TICKS(kDownlinkAudioQueueWaitMs));
  if (queued != pdTRUE) {
    DownlinkAudioWorkItem::release(raw);
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    g_runtime.diagnostics.last_binary_error = "downlink_audio_queue_full";
    g_runtime.diagnostics.last_error = g_runtime.diagnostics.last_binary_error;
    g_runtime.downlink_queue_drops += 1;
    ESP_LOGW(kTag, "downlink audio queue full; dropped frame drops=%" PRIu32,
             g_runtime.downlink_queue_drops);
    return false;
  }
  return true;
}

void flush_pending_mcp_messages() {
  // Return to session/control processing even under continuous MCP traffic.
  for (size_t sent = 0; sent < 4; ++sent) {
    std::string message;
    {
      std::lock_guard<std::mutex> lock(g_runtime.mutex);
      if (g_runtime.pending_mcp_messages.empty()) {
        return;
      }
      message = g_runtime.pending_mcp_messages.pop();
    }
    if (message.empty()) {
      continue;
    }
    if (send_service_text(message)) {
      std::lock_guard<std::mutex> lock(g_runtime.mutex);
      g_runtime.outgoing_mcp_messages += 1;
    }
  }
}

} // namespace lunaredu::pet::conversation::internal
