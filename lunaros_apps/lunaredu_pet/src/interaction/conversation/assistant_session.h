#pragma once

#include <mutex>

#include <string>
#include <vector>

#include "interaction/conversation/assistant_runtime_types.h"

namespace lunaredu::pet::conversation::internal {

enum class ConnectServiceAction : uint8_t {
  AlreadyInflight = 0,
  ReadyIdle,
  WaitAnnouncement,
  ReopenMqttAudioChannel,
  StartConnecting,
  ActivationIncomplete,
  MqttActivationIncomplete,
};

enum class PrepareStartListeningResult : uint8_t {
  ReadyToSend = 0,
  ReopenMqttAudioChannel,
  MissingSessionIdentity,
};

void handle_server_text_locked(const std::string &message, std::unique_lock<std::mutex> &runtime_lock);
void bind_service_ws_callbacks_locked();
void bind_service_mqtt_callbacks_locked();
ConnectServiceAction derive_connect_service_action_locked();
bool stop_listening_now_locked(const char *reason, bool count_stop_request, const char *idle_detail);
void stop_listening_for_microphone_mute();
PrepareStartListeningResult prepare_start_listening_locked(std::string &message);
void enter_listening_state_locked();
void commit_start_listening_success_locked();
void commit_start_listening_failure_locked();
bool prepare_stop_listening_locked(const char *reason, bool count_stop_request, const char *idle_detail, std::string &message, bool &send_required);
void commit_stop_listening_success_locked(const char *reason, const char *idle_detail, bool send_required);
void commit_stop_listening_failure_locked();
bool send_service_text(const std::string &message);
bool send_service_text_locked(const std::string &message);
bool send_service_binary(const std::vector<uint8_t> &message);
bool send_service_binary_locked(const std::vector<uint8_t> &message);
bool reopen_mqtt_audio_channel();
void flush_pending_mcp_messages();
void maybe_post_audio_playback_finished_locked(const char *source);
bool enqueue_downlink_audio(std::vector<uint8_t> frame);
void handle_server_binary(const std::vector<uint8_t> &frame);
void connect_task(void *argument);

} // namespace lunaredu::pet::conversation::internal
