#include "lunaredu_lunaros/persistence/system_audio.h"
#include "app/conversation_projection.h"

#include <cstring>
#include "interaction/transport/assistant_provider.h"

#include <ArduinoJson.h>

#include "lunaredu_lunaros/device_api/device_projection_json.h"
#include "lunaredu_lunaros/device_api/device_projection.h"
#include "lunaredu_lunaros/kernel/log.h"
#include "lunaredu_audio_bridge.h"
#include "lunaredu_pet_conversation.h"
#include "lunaredu_pet_conversation_policy.h"
#include "interaction/conversation/assistant_endpoint_policy.h"

namespace lunaredu::pet::conversation {

using namespace lunaredu::lunaros;
namespace {

constexpr const char *kCapabilityId = "xiaozhi_service";
constexpr const char *kModuleId = "xiaozhi";

constexpr const char *kPresenceCapabilities[] = {
    "xiaozhi",
    "voice_assistant",
    "native_ai_service",
};

const DeviceCapabilityDescriptor kXiaozhiCapability = {
    kCapabilityId,
    kPresenceCapabilities,
    sizeof(kPresenceCapabilities) / sizeof(kPresenceCapabilities[0]),
};

void fill_status(JsonObject payload) {
  const ServiceSnapshot status = snapshot();
  const AssistantPolicySnapshot policy = assistant_policy_snapshot();
  const auto audio = lunaredu::audio::local_pcm_audio_bridge_status();
  payload["ok"] = status.initialized && !status.failed;
  payload["assistant_platform"] = kAssistantUsesLunarLink ? "lunarlink" : "xiaozhi";
  payload["available"] = status.initialized;
  payload["initialized"] = status.initialized;
  payload["enabled"] = status.enabled;
  payload["suspended"] = status.suspended;
  payload["suspension_reason"] = status.suspension_reason;
  payload["running"] = status.running;
  payload["activation_ready"] = status.activation_ready;
  payload["activation_pending"] = status.activation_pending;
  payload["activation_endpoint"] = status.activation_endpoint;
  payload["activation_code"] = status.activation_code;
  payload["transport_connected"] = status.transport_connected;
  payload["session_state"] = status.session_state;
  payload["ready"] = status.ready;
  payload["listening"] = status.listening;
  payload["speaking"] = status.speaking;
  payload["opening"] = status.opening;
  payload["failed"] = status.failed;
  payload["last_error"] = status.last_error;
  payload["last_transport_error"] = status.last_transport_error;
  payload["audio_initialized"] = status.audio_initialized;
  payload["audio_input_enabled"] = status.audio_input_enabled;
  payload["audio_output_enabled"] = status.audio_output_enabled;
  payload["microphone_enabled"] = policy.microphone_enabled;
  payload["speaker_volume_percent"] = lunaredu::lunaros::system_volume_percent();
  payload["applied_speaker_volume_percent"] = audio.output_volume_percent;
  payload["input_sample_rate"] = status.input_sample_rate;
  payload["output_sample_rate"] = status.output_sample_rate;
  payload["input_failures"] = status.input_failures;
  payload["output_failures"] = status.output_failures;
  payload["incoming_audio_packets"] = status.incoming_audio_packets;
  payload["downlink_sequence_gaps"] = status.downlink_sequence_gaps;
  payload["downlink_stale_packets"] = status.downlink_stale_packets;
  payload["downlink_queue_drops"] = status.downlink_queue_drops;
  payload["downlink_decode_failures"] = status.downlink_decode_failures;
  payload["downlink_playback_failures"] = status.downlink_playback_failures;
  payload["output_frames"] = status.output_frames;
  payload["audio_error"] = status.audio_error;
  payload["wake_word"] = status.wake_word;
  payload["wake_word_model"] = status.wake_word_model;
  payload["wake_word_available"] = status.wake_word_available;
  payload["wake_word_enabled"] = status.wake_word_enabled;
  payload["standby_mode"] = status.standby_mode;
  payload["transcript_text"] = status.transcript_text;
  payload["transcript_role"] = status.transcript_role;
  payload["transcript_revision"] = status.transcript_revision;
  JsonArray wake_words = payload["available_wake_words"].to<JsonArray>();
  for (const auto &wake_word : status.available_wake_words) {
    wake_words.add(wake_word);
  }
}

void fill_capabilities(JsonObject payload) {
  payload["contract_version"] = 4;
  payload["native_service"] = true;
  payload["supports_enable_disable"] = true;
  payload["supports_temporary_suspension"] = true;
  payload["supports_product_persistence_callback"] = true;
  payload["supports_manual_listening"] = true;
  payload["supports_mcp_tools"] = !kAssistantUsesLunarLink;
  payload["assistant_platform"] = kAssistantUsesLunarLink ? "lunarlink" : "xiaozhi";
  payload["supports_device_capabilities"] = kAssistantUsesLunarLink;
  payload["supports_standby_mode"] = true;
  payload["supports_test_message"] = true;
  payload["supports_microphone_control"] = true;
  payload["supports_speaker_volume"] = true;
  JsonArray operations = payload["supported_ops"].to<JsonArray>();
  operations.add("status.get");
  operations.add("enabled.set");
  operations.add("listening.toggle");
  operations.add("tools.get");
  operations.add("standby_mode.set");
  operations.add("conversation.test");
  operations.add("microphone.enabled.set");
  operations.add("volume.set");
  operations.add("activation.start");
  operations.add("activation.clear");
  operations.add("activation.endpoint.set");
}

void fill_tools(JsonObject payload) {
  JsonArray tools = payload["tools"].to<JsonArray>();
  for (const ToolSnapshot &tool : tool_snapshots()) {
    JsonObject item = tools.add<JsonObject>();
    item["name"] = tool.name;
    item["description"] = tool.description;
    JsonDocument schema;
    if (deserializeJson(schema, tool.input_schema_json) ==
        DeserializationError::Ok) {
      item["input_schema"] = schema.as<JsonVariantConst>();
    } else {
      item["input_schema"]["type"] = "object";
    }
  }
  payload["count"] = tools.size();
}

DeviceOperationResult handle_enabled_set(JsonVariantConst request,
                                            JsonObject response) {
  JsonVariantConst enabled_value = request["enabled"];
  if (!enabled_value.is<bool>()) {
    return device_operation_error("missing_or_invalid_enabled", 400,
                                     "xiaozhi_control_failed");
  }
  if (!set_enabled(enabled_value.as<bool>())) {
    fill_status(response);
    return device_operation_error("xiaozhi_service_unavailable", 503,
                                     "xiaozhi_control_failed");
  }
  fill_status(response);
  return device_operation_ack();
}

DeviceOperationResult handle_listening_toggle(JsonVariantConst,
                                                 JsonObject response) {
  if (!toggle_listening()) {
    fill_status(response);
    return device_operation_error("xiaozhi_listening_toggle_rejected", 409,
                                     "xiaozhi_control_failed");
  }
  fill_status(response);
  return device_operation_ack(202);
}

DeviceOperationResult handle_microphone_enabled_set(JsonVariantConst request,
                                                     JsonObject response) {
  const JsonVariantConst enabled = request["enabled"];
  if (!enabled.is<bool>()) {
    return device_operation_error("missing_or_invalid_enabled", 400,
                                  "xiaozhi_control_failed");
  }
  if (!set_assistant_microphone_enabled(enabled.as<bool>())) {
    fill_status(response);
    return device_operation_error("microphone_preference_rejected", 500,
                                  "xiaozhi_control_failed");
  }
  fill_status(response);
  return device_operation_ack();
}

DeviceOperationResult handle_volume_set(JsonVariantConst request,
                                        JsonObject response) {
  const JsonVariantConst value = request["percent"];
  if (!value.is<int>()) {
    return device_operation_error("missing_or_invalid_volume", 400,
                                  "xiaozhi_control_failed");
  }
  const int percent = value.as<int>();
  if (percent < 0 || percent > 100 ||
      !lunaredu::lunaros::set_system_volume_percent(
          static_cast<uint8_t>(percent))) {
    fill_status(response);
    return device_operation_error("speaker_volume_rejected", 400,
                                  "xiaozhi_control_failed");
  }
  fill_status(response);
  return device_operation_ack();
}

DeviceOperationResult handle_activation_start(JsonVariantConst,
                                              JsonObject response) {
  if (!request_activation()) {
    fill_status(response);
    return device_operation_error("xiaozhi_activation_start_rejected", 503,
                                  "xiaozhi_control_failed");
  }
  fill_status(response);
  return device_operation_ack(202);
}

DeviceOperationResult handle_activation_clear(JsonVariantConst,
                                              JsonObject response) {
  if (!clear_activation()) {
    fill_status(response);
    return device_operation_error("xiaozhi_activation_clear_rejected", 503,
                                  "xiaozhi_control_failed");
  }
  fill_status(response);
  return device_operation_ack();
}

DeviceOperationResult handle_activation_endpoint_set(JsonVariantConst request,
                                                     JsonObject response) {
  if (!kAssistantUsesLunarLink) return device_operation_error("activation_endpoint_pinned", 409);
  if (!request["url"].is<const char *>())
    return device_operation_error("invalid_activation_endpoint", 400);
  const char *url = request["url"].as<const char *>();
  if (!internal::valid_activation_endpoint(url))
    return device_operation_error("invalid_activation_endpoint", 400);
  const auto before = snapshot();
  if (before.activation_ready || before.activation_pending)
    return device_operation_error("activation_endpoint_change_requires_unpair", 409);
  if (!set_activation_endpoint(url))
    return device_operation_error("activation_endpoint_write_failed", 500);
  fill_status(response);
  return device_operation_ack();
}

DeviceOperationResult handle_standby_mode_set(JsonVariantConst request,
                                               JsonObject response) {
  const char *mode = request["mode"] | "";
  StandbyMode next_mode;
  if (std::strcmp(mode, "wake_word") == 0) {
    next_mode = StandbyMode::WakeWord;
  } else if (std::strcmp(mode, "continuous_conversation") == 0) {
    next_mode = StandbyMode::ContinuousConversation;
  } else {
    return device_operation_error("missing_or_invalid_standby_mode", 400,
                                  "xiaozhi_control_failed");
  }
  if (!set_standby_mode(next_mode)) {
    fill_status(response);
    return device_operation_error("xiaozhi_standby_mode_rejected", 409,
                                  "xiaozhi_control_failed");
  }
  fill_status(response);
  return device_operation_ack(202);
}

DeviceOperationResult handle_conversation_test(JsonVariantConst request,
                                                JsonObject response) {
  const char *text = request["text"] | "你好";
  if (std::strlen(text) == 0 || std::strlen(text) > 64) {
    return device_operation_error("missing_or_invalid_test_text", 400,
                                  "xiaozhi_test_failed");
  }
  if (!send_test_message(text)) {
    fill_status(response);
    return device_operation_error("xiaozhi_test_channel_not_ready", 409,
                                  "xiaozhi_test_failed");
  }
  fill_status(response);
  return device_operation_ack(202);
}

void fill_contract(JsonObject contract) {
  contract["service_lifecycle"] = "native_freertos";
  contract["foreground_application_optional"] = true;
  contract["enabled_is_user_intent"] = true;
  contract["suspension_is_transient"] = true;
  contract["mcp_protocol_version"] = "2024-11-05";
}

constexpr DeviceOperationDefinition kOperations[] = {
    make_device_json_provider(
        "capabilities.get", fill_capabilities,
        lunaredu::lunaros::kNetworkServiceExecutor),
    make_device_json_provider("status.get", fill_status,
                              lunaredu::lunaros::kNetworkServiceExecutor),
    make_device_json_provider("tools.get", fill_tools,
                              lunaredu::lunaros::kNetworkServiceExecutor),
    make_device_json_command(
        "enabled.set", handle_enabled_set,
        device_operation_on_executor(
            lunaredu::lunaros::kNetworkServiceExecutor)),
    make_device_json_command(
        "listening.toggle", handle_listening_toggle,
        device_operation_on_executor(
            lunaredu::lunaros::kNetworkServiceExecutor)),
    make_device_json_command(
        "microphone.enabled.set", handle_microphone_enabled_set,
        device_operation_on_executor(
            lunaredu::lunaros::kNetworkServiceExecutor)),
    make_device_json_command(
        "volume.set", handle_volume_set,
        device_operation_on_executor(
            lunaredu::lunaros::kNetworkServiceExecutor)),
    make_device_json_command(
        "activation.start", handle_activation_start,
        device_operation_on_executor(
            lunaredu::lunaros::kNetworkServiceExecutor)),
    make_device_json_command(
        "activation.clear", handle_activation_clear,
        device_operation_on_executor(
            lunaredu::lunaros::kNetworkServiceExecutor)),
    make_device_json_command(
        "activation.endpoint.set", handle_activation_endpoint_set,
        device_operation_on_executor(
            lunaredu::lunaros::kNetworkServiceExecutor)),
    make_device_json_command(
        "standby_mode.set", handle_standby_mode_set,
        device_operation_on_executor(
            lunaredu::lunaros::kNetworkServiceExecutor)),
    make_device_json_command(
        "conversation.test", handle_conversation_test,
        device_operation_on_executor(
            lunaredu::lunaros::kNetworkServiceExecutor)),
};

const DeviceModuleProjectionMetadata kModule = {
    kModuleId,
    "native_ai_service",
    fill_contract,
};

const DeviceModuleProjection kProjection = {
    kCapabilityId,
    &kModule,
    kOperations,
    sizeof(kOperations) / sizeof(kOperations[0]),
};

const DeviceMetadataProjection kXiaozhiMetadata = {
    "xiaozhi",
    "xiaozhi",
    fill_status,
    nullptr,
};

} // namespace

bool install_device_projection(std::string &error) {
  if (!register_device_projection(
          {&kXiaozhiCapability, &kProjection, &kXiaozhiMetadata, false},
          &error)) {
    return false;
  }
  (void)deactivate_device_capability(kCapabilityId);
  return true;
}

bool set_device_projection_available(bool service_available,
                                     std::string &error) {
  error.clear();
  if (!service_available) {
    (void)deactivate_device_capability(kCapabilityId);
    return true;
  }
  return activate_device_capability(kXiaozhiCapability, &error);
}

} // namespace lunaredu::pet::conversation
