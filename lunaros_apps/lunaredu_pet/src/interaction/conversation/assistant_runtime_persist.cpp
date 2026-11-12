#include "interaction/transport/assistant_provider.h"
#include "interaction/conversation/assistant_core.h"
#include "interaction/conversation/assistant_loop.h"
#include "interaction/conversation/assistant_session.h"
#include "interaction/conversation/host_events.h"
#if ASSISTANT_USE_LUNARLINK
#include "interaction/conversation/assistant_endpoint_policy.h"
#include "interaction/conversation/assistant_runtime_constants.h"
#endif

#include <nvs.h>

namespace lunaredu::pet::conversation::internal {
namespace {

bool activation_ready_from_state(bool configured, bool pending) {
  return configured && !pending;
}

} // namespace

bool persist_string(const char *key, const std::string &value) {
  if (key == nullptr) {
    return false;
  }
  nvs_handle_t handle = 0;
  esp_err_t err = nvs_open(kActivationNamespace, NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    return false;
  }
  err = nvs_set_str(handle, key, value.c_str());
  if (err == ESP_OK) {
    err = nvs_commit(handle);
  }
  nvs_close(handle);
  return err == ESP_OK;
}

std::string restore_string(const char *key) {
  std::string value;
  if (key == nullptr) {
    return value;
  }
  nvs_handle_t handle = 0;
  esp_err_t err = nvs_open(kActivationNamespace, NVS_READONLY, &handle);
  if (err != ESP_OK) {
    return value;
  }
  size_t required_size = 0;
  err = nvs_get_str(handle, key, nullptr, &required_size);
  if (err != ESP_OK || required_size == 0) {
    nvs_close(handle);
    return value;
  }
  value.assign(required_size, '\0');
  err = nvs_get_str(handle, key, value.data(), &required_size);
  nvs_close(handle);
  if (err != ESP_OK) {
    value.clear();
    return value;
  }
  if (!value.empty() && value.back() == '\0') {
    value.pop_back();
  }
  return value;
}

void persist_state_locked() {
  nvs_handle_t handle = 0;
  esp_err_t err = nvs_open(kActivationNamespace, NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    ESP_LOGW(kTag, "persist_state open failed: %s", esp_err_to_name(err));
    return;
  }

  auto set_value = [&](const char *key, const std::string &value) {
    if (err != ESP_OK || key == nullptr) {
      return;
    }
    err = nvs_set_str(handle, key, value.c_str());
  };

#if ASSISTANT_USE_LUNARLINK
  set_value("ll_blocked", g_runtime.activation.user_blocked ? "1" : "0");
#else
  set_value(kPersistMqttEndpointKey, g_runtime.transport.mqtt_endpoint);
  set_value(kPersistMqttClientIdKey, g_runtime.transport.mqtt_client_id);
  set_value(kPersistMqttUserKey, g_runtime.transport.mqtt_username);
  set_value(kPersistMqttPasswordKey, g_runtime.transport.mqtt_password);
  set_value(kPersistMqttPublishKey, g_runtime.transport.mqtt_publish_topic);
  set_value(kPersistMqttSubscribeKey, g_runtime.transport.mqtt_subscribe_topic);

  set_value(kPersistCodeKey, g_runtime.activation.code);
  set_value(kPersistChallengeKey, g_runtime.activation.challenge);
  set_value(kPersistPendingKey, g_runtime.activation.pending ? "1" : "0");
  set_value(kPersistUserBlockedKey,
            g_runtime.activation.user_blocked ? "1" : "0");
  set_value(kPersistFreshPairingRequiredKey,
            g_runtime.activation.fresh_pairing_required ? "1" : "0");
  set_value(kPersistFreshPairingObservedKey,
            g_runtime.activation.fresh_pairing_observed ? "1" : "0");
  set_value(kPersistRemoteBindingActiveKey,
            g_runtime.activation.remote_binding_still_active ? "1" : "0");
  set_value(kPersistTimeoutKey,
            std::to_string(g_runtime.activation.timeout_ms));
  set_value(kPersistServerTimeKey,
            g_runtime.activation.has_server_time
                ? std::to_string(g_runtime.activation.server_time_ms)
                : std::string());
  set_value(kPersistTimezoneOffsetKey,
            std::to_string(g_runtime.activation.timezone_offset_minutes));
#endif
  set_value(kPersistStandbyModeKey,
            standby_listen_mode_name(g_runtime.conversation.standby_mode));
#if ASSISTANT_USE_LUNARLINK
  set_value(kPersistActivationOtaUrlKey, g_runtime.activation.ota_url);
#endif

  if (err == ESP_OK) {
    err = nvs_commit(handle);
  }
  nvs_close(handle);
  if (err != ESP_OK) {
    ESP_LOGW(kTag, "persist_state failed: %s", esp_err_to_name(err));
  }
}

void load_state_locked() {
#if ASSISTANT_USE_LUNARLINK
  g_runtime.activation.ota_url = restore_string(kPersistActivationOtaUrlKey);
  if (!valid_activation_endpoint(g_runtime.activation.ota_url)) {
    g_runtime.activation.ota_url.clear();
    g_runtime.diagnostics.last_error = "activation_endpoint_record_invalid";
  }
  g_runtime.activation.user_blocked = restore_string("ll_blocked") == "1";
  g_runtime.activation.configured = false;
  g_runtime.activation.ready = false;
  g_runtime.activation.pending = false;
#else
  g_runtime.transport.mqtt_endpoint = restore_string(kPersistMqttEndpointKey);
  g_runtime.transport.mqtt_client_id = restore_string(kPersistMqttClientIdKey);
  g_runtime.transport.mqtt_username = restore_string(kPersistMqttUserKey);
  g_runtime.transport.mqtt_password = restore_string(kPersistMqttPasswordKey);
  g_runtime.transport.mqtt_publish_topic = restore_string(kPersistMqttPublishKey);
  g_runtime.transport.mqtt_subscribe_topic = restore_string(kPersistMqttSubscribeKey);
  g_runtime.activation.code = restore_string(kPersistCodeKey);
  g_runtime.activation.challenge = restore_string(kPersistChallengeKey);
  const std::string pending = restore_string(kPersistPendingKey);
  const std::string user_blocked = restore_string(kPersistUserBlockedKey);
  const std::string fresh_pairing_required =
      restore_string(kPersistFreshPairingRequiredKey);
  const std::string fresh_pairing_observed =
      restore_string(kPersistFreshPairingObservedKey);
  const std::string remote_binding_active =
      restore_string(kPersistRemoteBindingActiveKey);
  const std::string timeout = restore_string(kPersistTimeoutKey);
  const std::string server_time = restore_string(kPersistServerTimeKey);
  const std::string timezone_offset = restore_string(kPersistTimezoneOffsetKey);
#endif
  const std::string standby_mode = restore_string(kPersistStandbyModeKey);
#if ASSISTANT_USE_LUNARLINK
  g_runtime.conversation.standby_mode = (standby_mode == "continuous" ||
      standby_mode == "continuous_conversation") ? StandbyListenMode::ContinuousConversation
                                                : StandbyListenMode::WakeWord;
#endif

#if !ASSISTANT_USE_LUNARLINK
  if (!timeout.empty()) {
    g_runtime.activation.timeout_ms =
        static_cast<uint32_t>(std::max(0, std::atoi(timeout.c_str())));
  }
  g_runtime.activation.pending = pending == "1";
  const bool no_prior_activation_state = user_blocked.empty() &&
      pending.empty() && g_runtime.activation.code.empty() &&
      g_runtime.transport.mqtt_endpoint.empty();
  // A factory-fresh device must wait for the child, teacher, or parent to
  // explicitly start the XiaoZhi step. Existing upgraded activations keep
  // working because their MQTT state proves that onboarding happened before.
  g_runtime.activation.user_blocked =
      user_blocked == "1" || no_prior_activation_state;
  // Upgrade devices that were locally unbound by the previous firmware into
  // the strict flow as well. Existing working MQTT activations remain valid.
  g_runtime.activation.fresh_pairing_required =
      fresh_pairing_required == "1" ||
      (fresh_pairing_required.empty() &&
       (g_runtime.activation.user_blocked || no_prior_activation_state));
  g_runtime.activation.fresh_pairing_observed =
      g_runtime.activation.fresh_pairing_required &&
      fresh_pairing_observed == "1";
  g_runtime.activation.remote_binding_still_active =
      remote_binding_active == "1";
  if (g_runtime.activation.remote_binding_still_active) {
    g_runtime.activation.user_blocked = true;
    g_runtime.diagnostics.last_error = kRemoteBindingStillActiveError;
  }
  if (!server_time.empty()) {
    g_runtime.activation.server_time_ms = std::atoll(server_time.c_str());
    g_runtime.activation.has_server_time = true;
  }
  if (!timezone_offset.empty()) {
    g_runtime.activation.timezone_offset_minutes =
        std::atoi(timezone_offset.c_str());
  }
  if (standby_mode == "continuous" ||
      standby_mode == "continuous_conversation") {
    g_runtime.conversation.standby_mode =
        StandbyListenMode::ContinuousConversation;
  } else {
    g_runtime.conversation.standby_mode = StandbyListenMode::WakeWord;
  }
  g_runtime.activation.configured = (!g_runtime.transport.mqtt_endpoint.empty());
  g_runtime.activation.ready = activation_ready_from_state(
      g_runtime.activation.configured, g_runtime.activation.pending);
#if ASSISTANT_USE_MAC
  // Always re-bootstrap this local host; never load official cloud credentials.
  g_runtime.transport.mqtt_endpoint.clear();
  g_runtime.transport.mqtt_password.clear();
  g_runtime.activation.configured = false;
  g_runtime.activation.ready = false;
  g_runtime.activation.pending = false;
  g_runtime.activation.user_blocked = user_blocked == "1";
  g_runtime.activation.fresh_pairing_required = false;
  g_runtime.activation.fresh_pairing_observed = false;
  g_runtime.activation.remote_binding_still_active = false;
  g_runtime.diagnostics.last_error.clear();
#endif
#endif
}

void apply_standby_listen_mode_locked(StandbyListenMode mode, bool persist_mode) {
  g_runtime.conversation.standby_mode = mode;
  if (mode == StandbyListenMode::ContinuousConversation) {
    g_runtime.wake_word.enabled = false;
    g_runtime.conversation.wake_word_resume_not_before_ms = 0;
    {
      std::lock_guard<std::mutex> ww_lock(g_runtime.wake_word_mutex);
      g_runtime.wake_word_frontend.stop();
    }
  } else {
    g_runtime.wake_word.enabled = true;
    g_runtime.conversation.wake_word_resume_not_before_ms =
        now_ms() + static_cast<int64_t>(kWakeWordResumeDelayMs);
  }
  if (persist_mode) {
    persist_state_locked();
  }
}

void toggle_standby_listen_mode() {
  (void)post_assistant_event(
      {AssistantEventType::ToggleStandbyMode, {}, {}, false});
}

bool request_standby_listen_mode(StandbyListenMode mode) {
  return post_assistant_event(
      {AssistantEventType::SetStandbyMode, {}, {},
       mode == StandbyListenMode::ContinuousConversation});
}

void apply_activation_result_locked(const XiaozhiActivationResult &result) {
  g_runtime.activation.code = result.activation_code;
  g_runtime.activation.challenge = result.activation_challenge;
  g_runtime.activation.timeout_ms =
      result.activation_timeout_ms > 0 ? result.activation_timeout_ms
                                       : kActivationTimeoutMsDefault;
  g_runtime.activation.pending = result.activation_pending;
  if (result.has_server_time) {
    g_runtime.activation.server_time_ms = result.server_time_ms;
    g_runtime.activation.has_server_time = true;
    g_runtime.activation.timezone_offset_minutes =
        result.timezone_offset_minutes;
  }
  g_runtime.transport.mqtt_endpoint = result.mqtt_endpoint;
  g_runtime.transport.mqtt_client_id = result.mqtt_client_id;
  g_runtime.transport.mqtt_username = result.mqtt_username;
  g_runtime.transport.mqtt_password = result.mqtt_password;
  g_runtime.transport.mqtt_publish_topic = result.mqtt_publish_topic;
  g_runtime.transport.mqtt_subscribe_topic = result.mqtt_subscribe_topic;
  g_runtime.activation.configured = result.has_mqtt && !result.mqtt_endpoint.empty();
  g_runtime.activation.ready = activation_ready_from_state(
      g_runtime.activation.configured, g_runtime.activation.pending);
  g_runtime.diagnostics.last_error = result.error;
  persist_state_locked();
}

} // namespace lunaredu::pet::conversation::internal
