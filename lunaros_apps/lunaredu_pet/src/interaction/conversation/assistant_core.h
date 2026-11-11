#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <ArduinoJson.h>
#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_random.h>
#include <esp_system.h>
#include <esp_timer.h>

#include "interaction/conversation/assistant_runtime_types.h"
#include "interaction/transport/assistant_transport.h"

namespace lunaredu::pet::conversation::internal {

inline constexpr const char *kTag = "assistant";

void refresh_wake_word_status_locked(
    const lunaredu::pet::conversation::WakeWordFrontendStatus &status);
int64_t now_ms();
uint32_t pcm_abs_i16(int16_t value);
int16_t clamp_pcm_i32(int32_t value);
void apply_pcm_gain(std::vector<int16_t> &samples, int numerator,
                    int denominator);
void remove_pcm_dc_bias(std::vector<int16_t> &samples);
void normalize_pcm_peak(std::vector<int16_t> &samples, uint32_t target_peak,
                        uint32_t min_peak, int max_gain_numerator,
                        int denominator);
std::string heap_snapshot_text();
std::string format_progress_counter_locked();
std::string derive_state_label_locked();
std::string derive_state_detail_locked();
std::string format_runtime_debug_result_locked();
bool session_ready_locked();
const char *reset_reason_name(esp_reset_reason_t reason);

const char *standby_listen_mode_name(StandbyListenMode mode);
void apply_standby_listen_mode_locked(StandbyListenMode mode,
                                      bool persist_mode);
bool request_standby_listen_mode(StandbyListenMode mode);
void toggle_standby_listen_mode();
bool persist_string(const char *key, const std::string &value);
std::string restore_string(const char *key);
void persist_state_locked();
void load_state_locked();
void set_session_state_locked(AssistantSessionState state);
bool session_transport_available_locked();
bool session_active_locked();
bool session_idle_available_locked();
bool session_waiting_for_announcement_locked();
bool session_identity_available_locked();
bool session_control_channel_present_locked();
bool session_control_channel_connected_locked();
bool session_control_path_ready_locked();
bool session_audio_path_ready_locked();
bool session_listening_locked();
bool session_start_inflight_locked();
bool session_connect_inflight_locked();
bool session_has_announcement_locked();
bool session_can_start_listening_locked();
bool session_can_finish_listening_locked();
bool session_can_stop_listening_locked();
bool session_can_abort_speaking_locked();
bool conversation_auto_resume_enabled_locked();
void set_session_start_inflight_locked(bool inflight);
void set_session_connect_inflight_locked(bool inflight);
void set_session_has_announcement_locked(bool announced);
bool session_should_send_listen_start_locked();
void session_reset_supporting_facts_locked();
void reset_session_locked(
    AssistantSessionState next_session_state = AssistantSessionState::Inactive);
void close_audio_channel_locked(
    const char *detail,
    AssistantSessionState next_session_state = AssistantSessionState::Inactive,
    bool release_focus = true);
bool ensure_opus_encoder_locked();
bool ensure_opus_decoder_locked(int sample_rate, int frame_duration_ms);

std::shared_ptr<NetworkMqttSession> take_service_mqtt_locked();
void apply_activation_result_locked(const XiaozhiActivationResult &result);
bool warmup_opus_codec_locked();
void drain_downlink_audio_queue();


} // namespace lunaredu::pet::conversation::internal
