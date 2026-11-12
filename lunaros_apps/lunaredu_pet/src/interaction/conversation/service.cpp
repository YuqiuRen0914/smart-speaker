#include "interaction/voice/resident_audio.h"
#include "runtime/conversation_state.h"
#include "interaction/conversation/suspension_policy.h"
#include "lunaredu_pet_conversation_events.h"
#include "lunaredu_pet_conversation.h"

#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>

#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "interaction/conversation/assistant_core.h"
#include "interaction/conversation/assistant_lifecycle.h"
#include "interaction/conversation/assistant_loop.h"
#include "interaction/conversation/assistant_endpoint_policy.h"
#include "interaction/conversation/assistant_runtime_constants.h"
#include "interaction/conversation/assistant_session.h"
#include "interaction/conversation/assistant_session_state.h"
#include "interaction/transport/network_xiaozhi_protocol.h"
#include "interaction/voice/local_audio_app.h"
#include "lunaredu_audio_bridge.h"
#include "interaction/conversation/host_events.h"
#include "app/conversation_projection.h"
#include "lunaredu_pet_conversation_policy.h"

namespace lunaredu::pet::conversation {
namespace {

constexpr const char *kTag = "pet.xiaozhi";
std::mutex g_setup_mutex;
std::mutex g_policy_callback_mutex;
void *g_policy_callback_context = nullptr;
void (*g_enabled_changed_callback)(void *, bool) = nullptr;
std::atomic<bool> g_projection_workers_ready{false};
std::mutex g_cloud_maintenance_mutex;
bool g_cloud_maintenance_active = false;
std::mutex g_suspension_mutex;
SuspensionPolicy g_suspensions;

} // namespace

static bool apply_suspensions_locked();

bool register_device_projection_surface(std::string &error) {
  return install_device_projection(error);
}

bool setup_service(const ServiceConfig &config) {
  std::lock_guard<std::mutex> setup_lock(g_setup_mutex);
  std::string projection_error;
  (void)set_device_projection_available(false, projection_error);
  g_projection_workers_ready.store(false, std::memory_order_release);
  conversation_set_host_callbacks(config.host);
  {
    std::lock_guard<std::mutex> callback_lock(g_policy_callback_mutex);
    g_policy_callback_context = config.policy_context;
    g_enabled_changed_callback = config.enabled_changed;
  }

  std::string tool_error;
  if (!register_application_tools(config.tools, tool_error)) {
    ESP_LOGE(kTag, "Pet capability tools unavailable: %s", tool_error.c_str());
    return false;
  }
  bool already_initialized = false;
  bool already_enabled = false;
  bool ready = false;
  {
    std::lock_guard<std::mutex> lock(lunaredu::pet::conversation::internal::g_runtime.mutex);
    already_initialized =
        lunaredu::pet::conversation::internal::g_runtime.service_initialized;
    already_enabled = lunaredu::pet::conversation::internal::g_runtime.service_enabled;
    if (!already_initialized) {
      lunaredu::pet::conversation::internal::g_runtime.service_enabled = config.enabled;
    }
  }
  ready = lunaredu::pet::conversation::internal::assistant_worker_tasks_ready();

  if (already_initialized) {
    if (already_enabled != config.enabled && !set_enabled(config.enabled)) {
      (void)set_device_projection_available(ready, projection_error);
      return false;
    }
    if (set_device_projection_available(ready, projection_error)) {
      g_projection_workers_ready.store(ready, std::memory_order_release);
    }
    return ready;
  }

  setup_local_audio_app();
  lunaredu::pet::conversation::internal::setup_assistant_minimal_app();

  {
    std::lock_guard<std::mutex> lock(lunaredu::pet::conversation::internal::g_runtime.mutex);
    lunaredu::pet::conversation::internal::g_runtime.service_initialized = true;
  }
  {
    std::lock_guard<std::mutex> transition(g_suspension_mutex);
    (void)apply_suspensions_locked();
  }
  ready = lunaredu::pet::conversation::internal::assistant_worker_tasks_ready();

  if (!config.enabled) {
    lunaredu::pet::conversation::audio_io::enable_input(false);
    lunaredu::pet::conversation::audio_io::enable_output(false);
  }
  if (!ready) {
    ESP_LOGE(kTag, "native service worker setup incomplete");
  }
  if (!set_device_projection_available(ready, projection_error)) {
    ESP_LOGE(kTag, "failed to update projection availability: %s",
             projection_error.c_str());
  } else {
    g_projection_workers_ready.store(ready, std::memory_order_release);
  }
  return ready;
}

void tick_service() {
  lunaredu::pet::conversation::internal::tick_assistant_minimal_app();
  // Read first, then update Pet: never nest the conversation and Pet mutexes.
  lunaredu::pet::runtime::update_conversation_phase(
      conversation_phase_from_snapshot(runtime_snapshot()));
  const bool ready = lunaredu::pet::conversation::internal::assistant_worker_tasks_ready();
  const bool projected =
      g_projection_workers_ready.load(std::memory_order_acquire);
  if (ready != projected) {
    std::string projection_error;
    if (set_device_projection_available(ready, projection_error)) {
      g_projection_workers_ready.store(ready, std::memory_order_release);
      ESP_LOGI(kTag, "native service workers recovered ready=%d", ready);
    } else {
      ESP_LOGE(kTag, "failed to refresh projection availability: %s",
               projection_error.c_str());
    }
  }
}

bool set_enabled(bool enabled) {
  std::shared_ptr<NetworkMqttSession> mqtt_session;
  void *policy_context = nullptr;
  void (*enabled_changed)(void *, bool) = nullptr;
  {
    std::lock_guard<std::mutex> lock(lunaredu::pet::conversation::internal::g_runtime.mutex);
    auto &runtime = lunaredu::pet::conversation::internal::g_runtime;
    if (!runtime.service_initialized) {
      return false;
    }
    if (runtime.service_enabled == enabled) {
      return true;
    }

    runtime.service_enabled = enabled;
    runtime.pending_events.clear();
    if (!enabled) {
      lunaredu::pet::conversation::internal::reset_session_locked(
          lunaredu::pet::conversation::internal::AssistantSessionState::Inactive);
      mqtt_session = lunaredu::pet::conversation::internal::take_service_mqtt_locked();
      runtime.close_audio_channel_requested.store(true);
    }
  }

  if (mqtt_session != nullptr) {
    mqtt_session->Disconnect();
    mqtt_session.reset();
  }

  {
    std::lock_guard<std::mutex> callback_lock(g_policy_callback_mutex);
    policy_context = g_policy_callback_context;
    enabled_changed = g_enabled_changed_callback;
  }
  if (enabled_changed != nullptr) {
    enabled_changed(policy_context, enabled);
  }

  if (enabled) {
    conversation_emit_status("已启用", "助手原生服务已开启", "等待网络连接",
                           nullptr);
  } else {
    conversation_emit_status("已关闭", "助手原生服务已暂停", "麦克风已释放",
                           nullptr);
  }
  ESP_LOGI(kTag, "service enabled=%d", enabled);
  return true;
}

bool clear_activation() {
  std::shared_ptr<NetworkMqttSession> mqtt_session;
  {
    std::lock_guard<std::mutex> lock(lunaredu::pet::conversation::internal::g_runtime.mutex);
    auto &runtime = lunaredu::pet::conversation::internal::g_runtime;
    if (runtime.service_initialized) {
      lunaredu::pet::conversation::internal::reset_session_locked(
          lunaredu::pet::conversation::internal::AssistantSessionState::Inactive);
      mqtt_session = lunaredu::pet::conversation::internal::take_service_mqtt_locked();
    }
    runtime.close_audio_channel_requested.store(true);
    runtime.pending_events.clear();
    runtime.conversation.pending_test_message.clear();
    runtime.conversation.pending_test_is_audio_retry = false;
    runtime.conversation.one_shot_test_text.clear();
    runtime.conversation.one_shot_test_audio_retry_count = 0;
    runtime.conversation.transcript_text.clear();
    runtime.conversation.transcript_role.clear();
    runtime.conversation.transcript_revision += 1;

    const std::string activation_ota_url = runtime.activation.ota_url;
    ++runtime.platform_generation;
    runtime.activation = {};
    runtime.activation.ota_url = activation_ota_url;
    runtime.activation.timeout_ms = lunaredu::pet::conversation::internal::kActivationTimeoutMsDefault;
    runtime.activation.user_blocked = true;
    runtime.activation.fresh_pairing_required = !kAssistantUsesLunarLink && !kAssistantUsesMac;
    runtime.activation.fresh_pairing_observed = false;
    runtime.activation.remote_binding_still_active = false;
    runtime.transport.connected = false;
    runtime.transport.session_announced = false;
    runtime.transport.connect_pending = false;
    runtime.transport.session_id.clear();
    runtime.transport.mqtt_endpoint.clear();
    runtime.transport.mqtt_client_id.clear();
    runtime.transport.mqtt_username.clear();
    runtime.transport.mqtt_password.clear();
    runtime.transport.mqtt_publish_topic.clear();
    runtime.transport.mqtt_subscribe_topic.clear();
    runtime.diagnostics.last_error.clear();
    runtime.diagnostics.last_transport_error.clear();
    lunaredu::pet::conversation::internal::persist_state_locked();
  }

  if (mqtt_session != nullptr) {
    mqtt_session->Disconnect();
  }
  lunaredu::pet::conversation::audio_io::enable_input(false);
  lunaredu::pet::conversation::audio_io::enable_output(false);
  conversation_emit_status("已解除配对", "本机 LunarLink 会话已清除", "等待重新配对",
                         nullptr);
  ESP_LOGI(kTag, "local LunarLink bootstrap cleared");
  return true;
}

bool request_activation() {
  std::lock_guard<std::mutex> lock(lunaredu::pet::conversation::internal::g_runtime.mutex);
  auto &runtime = lunaredu::pet::conversation::internal::g_runtime;
  if (!runtime.service_initialized) {
    return false;
  }
  if (runtime.activation.request_inflight || runtime.activation.ready) return true;
  ++runtime.platform_generation;
  runtime.activation.user_blocked = false;
  runtime.activation.pending = false;
  runtime.activation.request_inflight = false;
  runtime.activation.result_available = false;
  runtime.activation.result_ok = false;
  runtime.activation.result = XiaozhiActivationResult();
  runtime.activation.attempts = 0;
  runtime.activation.next_attempt_at_ms = 0;
  runtime.activation.remote_binding_still_active = false;
  runtime.diagnostics.last_error.clear();
  runtime.diagnostics.last_transport_error.clear();
  lunaredu::pet::conversation::internal::persist_state_locked();
  ESP_LOGI(kTag,
           "local LunarLink bootstrap explicitly requested fresh_required=%d fresh_seen=%d",
           runtime.activation.fresh_pairing_required,
           runtime.activation.fresh_pairing_observed);
  return true;
}

bool set_activation_endpoint(const char *url) {
  if (url == nullptr) return false;
  // Official XiaoZhi credentials are scoped to its pinned endpoint.
  if (!kAssistantUsesLunarLink) return false;
  std::lock_guard<std::mutex> lock(lunaredu::pet::conversation::internal::g_runtime.mutex);
  auto &runtime = lunaredu::pet::conversation::internal::g_runtime;
  if (!runtime.service_initialized) return false;
  const bool credentials_present = runtime.activation.configured ||
      !runtime.transport.mqtt_endpoint.empty() || !runtime.transport.mqtt_password.empty();
  const bool activation_pending = runtime.activation.pending || runtime.activation.request_inflight;
  return lunaredu::pet::conversation::internal::commit_activation_endpoint(
      url, credentials_present, activation_pending, runtime.activation.ota_url,
      lunaredu::pet::conversation::internal::persist_string);
}

bool set_suspension(SuspensionReason owner, bool active) {
  std::lock_guard<std::mutex> transition(g_suspension_mutex);
  g_suspensions.set(owner, active);
  return apply_suspensions_locked();
}

static bool apply_suspensions_locked() {
  const bool suspended = g_suspensions.suspended();
  std::shared_ptr<NetworkMqttSession> mqtt_session;
  std::string suspension_reason;
  bool service_enabled = false;
  const auto audio_policy = assistant_policy_snapshot();
  const bool preserve_foreground_audio =
      suspended && !audio_policy.audio_focus_owner.empty();
  {
    std::lock_guard<std::mutex> lock(lunaredu::pet::conversation::internal::g_runtime.mutex);
    auto &runtime = lunaredu::pet::conversation::internal::g_runtime;
    if (!runtime.service_initialized) return true;
    suspension_reason = g_suspensions.detail();
    if (runtime.service_suspended == suspended &&
        runtime.service_suspension_reason == suspension_reason) {
      return true;
    }

    runtime.service_suspended = suspended;
    runtime.service_suspension_reason = suspension_reason;
    service_enabled = runtime.service_enabled;
    std::erase_if(runtime.pending_events, [](const auto &event) {
      return !lunaredu::pet::conversation::internal::assistant_event_survives_suspension(
          event.type);
    });
    runtime.conversation.pending_test_message.clear();
    runtime.conversation.pending_test_is_audio_retry = false;
    runtime.conversation.one_shot_test_text.clear();
    runtime.conversation.one_shot_test_audio_retry_count = 0;
    if (suspended && !preserve_foreground_audio) {
      lunaredu::pet::conversation::internal::reset_session_locked(
          lunaredu::pet::conversation::internal::AssistantSessionState::Inactive);
      mqtt_session = lunaredu::pet::conversation::internal::take_service_mqtt_locked();
      runtime.close_audio_channel_requested.store(true);
    } else if (suspended) {
      // A foreground diagnostics lease owns the I2S bridge. Cloud maintenance
      // may still reset the assistant session, but must not close the bridge
      // underneath the foreground test.
      lunaredu::pet::conversation::internal::reset_session_locked(
          lunaredu::pet::conversation::internal::AssistantSessionState::Inactive);
      mqtt_session = lunaredu::pet::conversation::internal::take_service_mqtt_locked();
    }
  }

  if (mqtt_session != nullptr) {
    mqtt_session->Disconnect();
    mqtt_session.reset();
  }
  if (suspended && !preserve_foreground_audio) {
    // Discard packets queued before the foreground lease.  An item already
    // being decoded is rejected again by handle_server_binary() before it can
    // touch the shared I2S bridge.
    lunaredu::pet::conversation::internal::drain_downlink_audio_queue();
    lunaredu::pet::conversation::audio_io::enable_input(false);
    lunaredu::pet::conversation::audio_io::enable_output(false);
    conversation_emit_status("已暂停", "音频资源由前台应用使用", "",
                           suspension_reason.c_str());
  } else if (suspended && preserve_foreground_audio) {
    conversation_emit_status("已暂停", "音频资源由前台应用使用", "",
                           suspension_reason.c_str());
  } else if (service_enabled) {
    conversation_emit_status("恢复中", "助手正在恢复后台待命", "", nullptr);
  } else {
    conversation_emit_status("已关闭", "助手服务保持关闭", "", nullptr);
  }
  ESP_LOGI(kTag, "service suspended=%d reason=%s", suspended,
           suspension_reason.empty() ? "-" : suspension_reason.c_str());
  return true;
}

bool begin_cloud_maintenance(uint32_t timeout_ms) {
  std::lock_guard<std::mutex> operation(g_cloud_maintenance_mutex);
  if (g_cloud_maintenance_active) return false;
  g_cloud_maintenance_active = true;
  if (!set_suspension(SuspensionReason::CloudMaintenance, true)) {
    g_cloud_maintenance_active = false;
    return false;
  }
  // Only an explicit TLS maintenance operation is allowed to release ESP-SR.
  // Normal service suspension keeps the frontend resident so repeated
  // foreground/transport transitions do not fragment internal SRAM.
  if (!lunaredu::pet::conversation::internal::request_wake_word_frontend_release()) {
    ESP_LOGW(kTag, "cloud maintenance could not schedule AFE release");
    g_cloud_maintenance_active = false;
    (void)set_suspension(SuspensionReason::CloudMaintenance, false);
    return false;
  }
  const int64_t deadline =
      static_cast<int64_t>(esp_timer_get_time() / 1000ULL) + timeout_ms;
  for (;;) {
    bool afe_ready = false;
    bool init_in_progress = false;
    bool release_in_progress = false;
    {
      std::lock_guard<std::mutex> lock(lunaredu::pet::conversation::internal::g_runtime.mutex);
      afe_ready = lunaredu::pet::conversation::internal::g_runtime.wake_word.afe_initialized;
      init_in_progress =
          lunaredu::pet::conversation::internal::g_runtime.wake_word.afe_init_in_progress ||
          lunaredu::pet::conversation::internal::g_runtime.wake_word_init_task != nullptr;
      release_in_progress =
          lunaredu::pet::conversation::internal::g_runtime.wake_word.afe_release_in_progress ||
          lunaredu::pet::conversation::internal::g_runtime.wake_word_release_task != nullptr;
    }
    if (!afe_ready && !init_in_progress && !release_in_progress) {
      return true;
    }
    if (static_cast<int64_t>(esp_timer_get_time() / 1000ULL) >= deadline) {
      ESP_LOGW(kTag, "cloud maintenance timed out waiting for AFE release");
      // Entry is transactional: callers invoke end_cloud_maintenance() only
      // after a successful return. Roll back the suspension created above,
      // otherwise one timed-out TLS recovery leaves XiaoZhi permanently
      // suspended and the UI reports an endless reconnect.
      g_cloud_maintenance_active = false;
      (void)set_suspension(SuspensionReason::CloudMaintenance, false);
      return false;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void end_cloud_maintenance() {
  std::lock_guard<std::mutex> operation(g_cloud_maintenance_mutex);
  if (!g_cloud_maintenance_active) return;
  (void)set_suspension(SuspensionReason::CloudMaintenance, false);
  g_cloud_maintenance_active = false;
}

bool request_listening() {
  return internal::request_conversation_control(true);
}

bool cancel_conversation() {
  return internal::request_conversation_control(false);
}

bool toggle_listening() {
  using lunaredu::pet::conversation::internal::AssistantEvent;
  using lunaredu::pet::conversation::internal::AssistantEventType;
  using lunaredu::pet::conversation::internal::AssistantSessionState;

  {
    std::lock_guard<std::mutex> lock(lunaredu::pet::conversation::internal::g_runtime.mutex);
    if (!lunaredu::pet::conversation::internal::g_runtime.service_initialized ||
        !lunaredu::pet::conversation::internal::g_runtime.service_enabled ||
        lunaredu::pet::conversation::internal::g_runtime.service_suspended) {
      return false;
    }

    const auto state = lunaredu::pet::conversation::internal::g_runtime.session_state;
    if (state == AssistantSessionState::Listening) {
      // stop_listening_now_locked publishes outside the runtime mutex.
    } else if (state == AssistantSessionState::Speaking) {
      return lunaredu::pet::conversation::internal::post_assistant_event_locked(
          {AssistantEventType::AbortSpeakingRequested, "manual_abort", {},
           false});
    } else {
      return lunaredu::pet::conversation::internal::post_assistant_event_locked(
          {AssistantEventType::ManualStartRequested, "manual", {}, false});
    }
  }

  return lunaredu::pet::conversation::internal::stop_listening_now_locked(
      "manual_stop", true, "manual listening stopped");
}

bool finish_listening() {
  using lunaredu::pet::conversation::internal::AssistantEvent;
  using lunaredu::pet::conversation::internal::AssistantEventType;
  using lunaredu::pet::conversation::internal::AssistantSessionState;

  {
    std::lock_guard<std::mutex> lock(lunaredu::pet::conversation::internal::g_runtime.mutex);
    if (!lunaredu::pet::conversation::internal::g_runtime.service_initialized ||
        !lunaredu::pet::conversation::internal::g_runtime.service_enabled ||
        lunaredu::pet::conversation::internal::g_runtime.service_suspended) {
      return false;
    }

    const auto state = lunaredu::pet::conversation::internal::g_runtime.session_state;
    if (state == AssistantSessionState::Listening) {
      // stop_listening_now_locked publishes outside the runtime mutex.
    } else if (state == AssistantSessionState::Opening) {
      return true;
    } else if (state == AssistantSessionState::Speaking) {
      return lunaredu::pet::conversation::internal::post_assistant_event_locked(
          {AssistantEventType::AbortSpeakingRequested, "manual_abort", {},
           false});
    } else {
      return false;
    }
  }

  return lunaredu::pet::conversation::internal::stop_listening_now_locked(
      "manual_finish", true, "manual listening finished");
}

bool set_standby_mode(StandbyMode mode) {
  using lunaredu::pet::conversation::internal::StandbyListenMode;
  if (mode != StandbyMode::WakeWord &&
      mode != StandbyMode::ContinuousConversation) {
    return false;
  }
  return lunaredu::pet::conversation::internal::request_standby_listen_mode(
      mode == StandbyMode::ContinuousConversation
          ? StandbyListenMode::ContinuousConversation
          : StandbyListenMode::WakeWord);
}

bool send_test_message(const char *text) {
  const std::string message_text = text == nullptr || *text == '\0' ? "你好" : text;
  std::string session_id;
  bool send_now = false;
  {
    std::lock_guard<std::mutex> lock(lunaredu::pet::conversation::internal::g_runtime.mutex);
    auto &runtime = lunaredu::pet::conversation::internal::g_runtime;
    if (!runtime.service_enabled || runtime.service_suspended ||
        !assistant_network_connected() || !runtime.activation.ready) {
      lunaredu::pet::conversation::internal::g_runtime.diagnostics.last_error =
          "xiaozhi_test_channel_not_ready";
      return false;
    }
    auto &conversation = runtime.conversation;
    const bool is_audio_recovery_retry =
        conversation.pending_test_is_audio_retry &&
        conversation.pending_test_message == message_text;
    conversation.pending_test_message.clear();
    conversation.pending_test_is_audio_retry = false;
    if (!is_audio_recovery_retry) {
      conversation.one_shot_test_text = message_text;
      conversation.one_shot_test_audio_retry_count = 0;
    }
    if (lunaredu::pet::conversation::internal::session_ready_locked() &&
        !runtime.transport.session_id.empty()) {
      send_now = true;
      session_id = runtime.transport.session_id;
      conversation.transcript_text = message_text;
      conversation.transcript_role = "user";
      conversation.transcript_revision += 1;
      conversation.awaiting_response = true;
      conversation.one_shot_test_active = true;
    } else {
      conversation.pending_test_message = message_text;
      conversation.pending_test_is_audio_retry = is_audio_recovery_retry;
      const auto state = runtime.session_state;
      if (!runtime.conversation.start_pending &&
          !runtime.transport.connect_pending &&
          state != lunaredu::pet::conversation::internal::AssistantSessionState::Opening &&
          state != lunaredu::pet::conversation::internal::AssistantSessionState::Listening &&
          state != lunaredu::pet::conversation::internal::AssistantSessionState::Speaking) {
        (void)lunaredu::pet::conversation::internal::post_assistant_event_locked(
            // A queued text test only needs a session channel. Starting the
            // manual microphone path as well races its listen-start message
            // against the queued text and leaves the first test stuck in
            // Listening. SessionHelloReceived sends the pending text after
            // this connection-only request completes.
            {lunaredu::pet::conversation::internal::AssistantEventType::ConnectRequested,
             message_text, "queued_test_message", false});
      }
      conversation_emit_status("连接中", "正在准备助手测试", message_text.c_str(),
                             nullptr);
    }
  }
  if (!send_now) return true;
  const bool focus_acquired = conversation_request_audio_focus("text_test", 500);
  const std::string request = xiaozhi_build_listen_detect_message(
      session_id, message_text);
  if (focus_acquired && lunaredu::pet::conversation::internal::send_service_text(request)) {
    return true;
  }
  if (focus_acquired) conversation_release_audio_focus("text_test_send_failed");
  std::lock_guard<std::mutex> lock(lunaredu::pet::conversation::internal::g_runtime.mutex);
  if (!focus_acquired)
    lunaredu::pet::conversation::internal::g_runtime.diagnostics.last_error =
        "assistant_audio_focus_unavailable";
  auto &conversation = lunaredu::pet::conversation::internal::g_runtime.conversation;
  conversation.one_shot_test_active = false;
  conversation.awaiting_response = false;
  conversation.one_shot_test_text.clear();
  conversation.one_shot_test_audio_retry_count = 0;
  return false;
}

ServiceRuntimeSnapshot runtime_snapshot() {
  ServiceRuntimeSnapshot result;
  bool service_initialized = false;
  {
    std::lock_guard<std::mutex> lock(lunaredu::pet::conversation::internal::g_runtime.mutex);
    const auto &runtime = lunaredu::pet::conversation::internal::g_runtime;
    service_initialized = runtime.service_initialized;
    result.enabled = runtime.service_enabled;
    result.suspended = runtime.service_suspended;
    result.activation_ready = runtime.activation.ready;
    result.activation_pending = runtime.activation.pending;
    result.transport_connected = runtime.transport.connected;
    result.ready = runtime.session_state ==
                       lunaredu::pet::conversation::internal::AssistantSessionState::ReadyIdle &&
                   !runtime.conversation.awaiting_response &&
                   lunaredu::pet::conversation::internal::session_ready_locked();
    result.listening = runtime.session_state ==
                       lunaredu::pet::conversation::internal::AssistantSessionState::Listening;
    result.thinking = runtime.conversation.awaiting_response;
    result.speaking = runtime.session_state ==
                      lunaredu::pet::conversation::internal::AssistantSessionState::Speaking;
    result.opening = runtime.conversation.control_pending || runtime.conversation.start_pending || runtime.session_state ==
                     lunaredu::pet::conversation::internal::AssistantSessionState::Opening;
    result.interaction_active = runtime.conversation.control_pending ||
        runtime.conversation.start_pending || result.listening || result.thinking || result.speaking ||
        runtime.conversation.one_shot_test_active;
    result.failed = runtime.session_state ==
                    lunaredu::pet::conversation::internal::AssistantSessionState::Failed;
  }
  result.initialized = service_initialized &&
                       lunaredu::pet::conversation::internal::assistant_worker_tasks_ready();
  return result;
}

ServiceSnapshot snapshot() {
  ServiceSnapshot result;
  bool service_initialized = false;
  {
    std::lock_guard<std::mutex> lock(lunaredu::pet::conversation::internal::g_runtime.mutex);
    const auto &runtime = lunaredu::pet::conversation::internal::g_runtime;
    service_initialized = runtime.service_initialized;
    result.enabled = runtime.service_enabled;
    result.suspended = runtime.service_suspended;
    result.suspension_reason = runtime.service_suspension_reason;
    result.running = runtime.service_enabled && !runtime.service_suspended;
    result.activation_ready = runtime.activation.ready;
    result.activation_pending = runtime.activation.pending;
    result.activation_code = runtime.activation.code;
    result.activation_endpoint = runtime.activation.ota_url.empty()
        ? lunaredu::pet::conversation::internal::kDefaultAssistantOtaUrl
        : runtime.activation.ota_url;
    result.transport_connected = runtime.transport.connected;
    // ReadyIdle is the transport state used while the cloud is preparing a
    // reply. Preserve that internal state machine, but expose the actionable
    // semantic phase so the screen does not fall back to an empty/idle label
    // after the user's transcript hold expires.
    result.session_state = runtime.conversation.awaiting_response
                               ? "thinking"
                               : lunaredu::pet::conversation::internal::assistant_session_state_name(
                                     runtime.session_state);
    result.ready = runtime.session_state ==
                       lunaredu::pet::conversation::internal::AssistantSessionState::ReadyIdle &&
                   !runtime.conversation.awaiting_response &&
                   lunaredu::pet::conversation::internal::session_ready_locked();
    result.listening = runtime.session_state ==
                       lunaredu::pet::conversation::internal::AssistantSessionState::Listening;
    result.speaking = runtime.session_state ==
                      lunaredu::pet::conversation::internal::AssistantSessionState::Speaking;
    result.opening = runtime.conversation.control_pending || runtime.conversation.start_pending || runtime.session_state ==
                     lunaredu::pet::conversation::internal::AssistantSessionState::Opening;
    result.failed = runtime.session_state ==
                    lunaredu::pet::conversation::internal::AssistantSessionState::Failed;
    result.last_error = runtime.diagnostics.last_error;
    result.last_transport_error = runtime.diagnostics.last_transport_error;
    result.wake_word = runtime.wake_word.requested_word;
    result.wake_word_model = runtime.wake_word.model_name;
    result.available_wake_words = runtime.wake_word.available_words;
    result.wake_word_available = runtime.wake_word.requested_word_available;
    result.wake_word_enabled = runtime.wake_word.enabled;
    result.standby_mode =
        lunaredu::pet::conversation::internal::standby_listen_mode_name(runtime.conversation.standby_mode);
    result.transcript_text = runtime.conversation.transcript_text;
    result.transcript_role = runtime.conversation.transcript_role;
    result.transcript_revision = runtime.conversation.transcript_revision;
  }
  result.initialized = service_initialized &&
                       lunaredu::pet::conversation::internal::assistant_worker_tasks_ready();

  const auto audio = lunaredu::audio::local_pcm_audio_bridge_status();
  result.audio_initialized = audio.initialized;
  result.audio_input_enabled = audio.input_enabled;
  result.audio_output_enabled = audio.output_enabled;
  result.input_sample_rate = audio.input_sample_rate;
  result.output_sample_rate = audio.output_sample_rate;
  result.input_failures = audio.input_failures;
  result.output_failures = audio.output_failures;
  {
    std::lock_guard<std::mutex> lock(lunaredu::pet::conversation::internal::g_runtime.mutex);
    result.incoming_audio_packets = lunaredu::pet::conversation::internal::g_runtime.incoming_audio_packets;
    result.downlink_sequence_gaps =
        lunaredu::pet::conversation::internal::g_runtime.downlink_sequence_gaps;
    result.downlink_stale_packets =
        lunaredu::pet::conversation::internal::g_runtime.downlink_stale_packets;
    result.downlink_queue_drops =
        lunaredu::pet::conversation::internal::g_runtime.downlink_queue_drops;
    result.downlink_decode_failures =
        lunaredu::pet::conversation::internal::g_runtime.downlink_decode_failures;
    result.downlink_playback_failures =
        lunaredu::pet::conversation::internal::g_runtime.downlink_playback_failures;
  }
  result.output_frames = audio.output_frames;
  result.audio_error = audio.last_error;
  return result;
}

std::vector<ToolSnapshot> tool_snapshots() {
  std::vector<ToolSnapshot> result;
  {
    std::lock_guard<std::mutex> lock(lunaredu::pet::conversation::internal::g_runtime.mutex);
    for (const auto &tool : lunaredu::pet::conversation::internal::g_runtime.external_tools) {
      result.push_back(
          {tool.name, tool.description, tool.input_schema_json});
    }
  }
  return result;
}

bool register_tool(const ToolRegistration &tool) {
  if (tool.name.empty() || tool.description.empty() ||
      tool.input_schema_json.empty() || tool.handler == nullptr) {
    ESP_LOGW(kTag, "[DEBUG-MCP-LINK] rejected invalid tool registration name=%s",
             tool.name.c_str());
    return false;
  }
  std::lock_guard<std::mutex> lock(lunaredu::pet::conversation::internal::g_runtime.mutex);
  auto &tools = lunaredu::pet::conversation::internal::g_runtime.external_tools;
  for (auto &existing : tools) {
    if (existing.name == tool.name) {
      existing = tool;
      ESP_LOGD(kTag, "[DEBUG-MCP-LINK] tool replaced name=%s",
               tool.name.c_str());
      return true;
    }
  }
  tools.push_back(tool);
  ESP_LOGD(kTag, "[DEBUG-MCP-LINK] tool registered name=%s total=%u",
           tool.name.c_str(), static_cast<unsigned>(tools.size()));
  return true;
}

} // namespace lunaredu::pet::conversation
