#pragma once

#include <array>
#include <atomic>
#include <cinttypes>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <esp_audio_enc.h>
#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/idf_additions.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "interaction/conversation/assistant_runtime_constants.h"
#include "interaction/voice/assistant_audio_pool.h"
#include "interaction/tools/assistant_mcp_queue.h"
#include "interaction/conversation/assistant_events.h"
#include "interaction/voice/opus_audio_codec.h"
#include "interaction/voice/wake_word_frontend.h"
#include "interaction/transport/assistant_transport.h"
#include <ArduinoJson.h>

#include "interaction/conversation/assistant_session_state.h"
#include "interaction/transport/assistant_activation_client.h"
#include "lunaredu_pet_conversation.h"

namespace lunaredu::pet::conversation::internal {


enum class StandbyListenMode : uint8_t {
  WakeWord = 0,
  ContinuousConversation
};



static_assert(DownlinkAudioPool::kCapacity >= kDownlinkAudioQueueDepth + 2);

struct ActivationState {
  bool configured = false;
  bool ready = false;
  bool pending = false;
  bool user_blocked = false;
  // A local unbind must not silently accept credentials that the XiaoZhi
  // server reissues for the former account. Require evidence of a new pairing
  // challenge before accepting a ready MQTT configuration again.
  bool fresh_pairing_required = false;
  bool fresh_pairing_observed = false;
  bool remote_binding_still_active = false;
  bool request_inflight = false;
  bool result_available = false;
  bool result_ok = false;
  XiaozhiActivationResult result;
  std::string code;
  std::string challenge;
  uint32_t timeout_ms = 0;
  uint32_t attempts = 0;
  int64_t next_attempt_at_ms = 0;
  int64_t server_time_ms = 0;
  bool has_server_time = false;
  int timezone_offset_minutes = 480;
};

struct TransportState {
  bool connected = false;
  bool session_announced = false;
  bool connect_pending = false;
  int64_t connect_started_at_ms = 0;
  int64_t last_service_activity_at_ms = 0;
  int64_t last_keepalive_sent_at_ms = 0;
  uint32_t audio_uplink_timestamp_ms = 0;
  std::string session_id;
  std::string mqtt_endpoint;
  std::string mqtt_client_id;
  std::string mqtt_username;
  std::string mqtt_password;
  std::string mqtt_publish_topic;
  std::string mqtt_subscribe_topic;
  int server_output_sample_rate = 24000;
  int server_output_frame_duration_ms = 60;

  std::shared_ptr<NetworkMqttSession> mqtt_session;
  std::shared_ptr<NetworkUdpChannel> udp_channel;
  XiaozhiMqttUdpChannel mqtt_udp_channel;
};

struct ConversationState {
  AssistantSessionState session_state = AssistantSessionState::Inactive;
  bool start_pending = false;
  std::string listening_mode = "manual";
  StandbyListenMode standby_mode = StandbyListenMode::WakeWord;
  int64_t idle_entered_at_ms = 0;
  int64_t listening_started_at_ms = 0;
  int64_t speaking_started_at_ms = 0;
  int64_t local_audio_capture_not_before_ms = 0;
  int64_t last_wake_word_trigger_at_ms = 0;
  int64_t wake_word_resume_not_before_ms = 0;
  int64_t session_close_deadline_ms = 0;
  std::string last_tts_state;
  std::string last_stop_result;
  bool awaiting_response = false;
  // A device-side test may be requested before the MQTT/UDP session exists.
  // Keep it in the assistant-owned session state and send it after hello.
  std::string pending_test_message;
  bool prime_uplink_requested = false;
  bool pending_test_is_audio_retry = false;
  // A conversation.test request is exactly one response turn. It must never
  // inherit continuous-conversation auto-resume after playback completes.
  bool one_shot_test_active = false;
  // Retain the request across one audio-channel rebuild. MQTT subtitles and
  // UDP audio are separate paths, so a test may receive text without audio.
  std::string one_shot_test_text;
  uint8_t one_shot_test_audio_retry_count = 0;
  bool tts_stop_waiting_for_playback = false;
  int64_t tts_stop_playback_grace_deadline_ms = 0;
  bool downlink_audio_active = false;
  std::string transcript_text;
  std::string transcript_role;
  uint32_t transcript_revision = 0;
};

struct DiagnosticsState {
  std::string last_error;
  std::string last_transport_error;
  std::string last_binary_error;
  std::string last_playback_error;
  std::string last_message_summary;
};

struct VoiceActivityState {
  bool voice_activity_active = false;
  uint32_t pcm_frames = 0;
  uint32_t speech_frames = 0;
  uint32_t silence_frames = 0;
  uint32_t last_rms = 0;
  uint32_t last_peak = 0;
  uint32_t last_raw_rms = 0;
  uint32_t last_raw_peak = 0;
  int32_t last_raw_mean = 0;
  uint32_t auto_stops = 0;
  uint32_t current_utterance_ms = 0;
  uint32_t speech_ms = 0;
  uint32_t silence_ms = 0;
  bool utterance_started = false;
};

struct WakeWordState {
  bool enabled = true;
  bool detection_in_listening = false;
  std::string requested_word = kRequestedWakeWord;
  std::string model_name;
  std::vector<std::string> available_words;
  bool requested_word_available = false;
  std::string afe_last_error;
  bool afe_initialized = false;
  bool afe_init_in_progress = false;
  bool afe_release_in_progress = false;
  uint32_t afe_init_attempts = 0;
  int64_t afe_next_init_at_ms = 0;
};

struct Runtime {
  std::mutex mutex;
  std::atomic<bool> close_audio_channel_requested{false};
  std::atomic<bool> routes_registered{false};
  bool service_initialized = false;
  bool service_enabled = false;
  bool service_suspended = false;
  std::string service_suspension_reason;
  uint32_t boot_id = 0;
  int64_t started_at_ms = 0;
  std::string device_id;
  std::string client_id;
  ActivationState activation;
  TransportState transport;
  ConversationState conversation;
  DiagnosticsState diagnostics;
  VoiceActivityState vad;
  WakeWordState wake_word;
  AssistantSessionState &session_state;
  uint32_t sent_audio_packets = 0;
  uint32_t listening_sent_audio_packets_base = 0;
  uint32_t incoming_audio_packets = 0;
  uint32_t downlink_sequence_gaps = 0;
  uint32_t downlink_stale_packets = 0;
  uint32_t downlink_queue_drops = 0;
  uint32_t downlink_decode_failures = 0;
  uint32_t downlink_playback_failures = 0;
  uint32_t speaking_incoming_audio_packets_base = 0;
  uint32_t outgoing_mcp_messages = 0;
  PendingMcpMessages pending_mcp_messages;
  std::deque<AssistantEvent> pending_events;
  std::vector<lunaredu::pet::conversation::ToolRegistration> external_tools;

  QueueHandle_t downlink_audio_queue = nullptr;
  DownlinkAudioPool downlink_audio_pool;
  TaskHandle_t audio_task = nullptr;
  TaskHandle_t connect_task = nullptr;
  TaskHandle_t wake_word_init_task = nullptr;
  TaskHandle_t wake_word_release_task = nullptr;
  OpusAudioCodec codec;
  std::mutex wake_word_mutex;
  lunaredu::pet::conversation::WakeWordFrontend wake_word_frontend;
  JsonDocument rx_doc;
  JsonDocument tx_doc;

  Runtime();
};

extern Runtime g_runtime;

} // namespace lunaredu::pet::conversation::internal
