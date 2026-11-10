#include "interaction/voice/resident_audio.h"
#include "interaction/conversation/assistant_session_internal.h"
#include "interaction/conversation/assistant_core.h"
#include "interaction/conversation/assistant_loop.h"
#include "interaction/conversation/assistant_lifecycle.h"
#include "interaction/transport/assistant_transport.h"
#include "interaction/conversation/host_events.h"

#include "lunaredu_audio_bridge.h"

#include <algorithm>
#include <cinttypes>

namespace lunaredu::pet::conversation::internal {

namespace {

// TLS/PSA setup needs a contiguous internal working block even though the
// MQTT buffers and task stack live in PSRAM.  Do not tear down the resident
// wake frontend speculatively: ordinary reconnects and cloud maintenance are
// common, while repeated ESP-SR destroy/create cycles fragment internal SRAM.
// Only retry with the frontend released after an actual MQTT allocation or
// handshake failure.
void release_wake_frontend_for_transport() {
  if (!request_wake_word_frontend_release()) {
    return;
  }
  (void)lunaredu::pet::conversation::audio_io::enable_input(false);
  (void)lunaredu::pet::conversation::audio_io::enable_output(false);
  for (uint32_t waited_ms = 0; waited_ms < 2000; waited_ms += 10) {
    bool in_progress = false;
    {
      std::lock_guard<std::mutex> lock(g_runtime.mutex);
      in_progress = g_runtime.wake_word.afe_release_in_progress ||
                    g_runtime.wake_word_release_task != nullptr;
    }
    if (!in_progress) {
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  ESP_LOGI(kTag, "released wake frontend before MQTT/TLS connect");
}

bool reopen_mqtt_audio_channel_locked_impl() {
  if (!session_control_channel_connected_locked()) {
    return false;
  }
  return true;
}

} // namespace



void bind_service_mqtt_callbacks_locked() {
  if (!g_runtime.transport.mqtt_session) {
    return;
  }
  g_runtime.transport.mqtt_session->OnConnected([]() {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    (void)post_assistant_event_locked(
        {AssistantEventType::TransportConnectedWaitSession, {}, {}, false});
  });
  g_runtime.transport.mqtt_session->OnDisconnected([]() {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    (void)post_assistant_event_locked(
        {AssistantEventType::TransportDisconnected, "MQTT 已断开，正在重连", {},
         false});
  });
  g_runtime.transport.mqtt_session->OnError([](const std::string &message) {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    g_runtime.diagnostics.last_transport_error = message;
    g_runtime.diagnostics.last_error = g_runtime.diagnostics.last_transport_error;
    (void)post_assistant_event_locked(
        {AssistantEventType::TransportError, message, {}, false});
  });
  g_runtime.transport.mqtt_session->OnMessage(
      [](const std::string &topic, const std::string &payload) {
        (void)topic;
        std::unique_lock<std::mutex> lock(g_runtime.mutex);
        handle_server_text_locked(payload, lock);
      });
}

ConnectServiceAction derive_connect_service_action_locked() {
  if (session_connect_inflight_locked()) {
    return ConnectServiceAction::AlreadyInflight;
  }
  if (session_control_channel_connected_locked()) {
    if (session_identity_available_locked() &&
        g_runtime.transport.udp_channel != nullptr &&
        !g_runtime.close_audio_channel_requested.load()) {
      return ConnectServiceAction::ReadyIdle;
    }
    return ConnectServiceAction::ReopenMqttAudioChannel;
  }
  if (g_runtime.transport.mqtt_endpoint.empty() ||
      g_runtime.transport.mqtt_publish_topic.empty()) {
    return ConnectServiceAction::MqttActivationIncomplete;
  }
  return ConnectServiceAction::StartConnecting;
}

bool reopen_mqtt_audio_channel() {
  std::shared_ptr<NetworkMqttSession> mqtt;
  {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    if (!reopen_mqtt_audio_channel_locked_impl()) {
      return false;
    }
    // Retain the session while publishing. Cloud maintenance may remove the
    // runtime-owned reference as soon as this lock is released.
    mqtt = g_runtime.transport.mqtt_session;
  }

  if (mqtt == nullptr || !mqtt->Publish(xiaozhi_build_mqtt_hello_message())) {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    (void)post_assistant_event_locked(
        {AssistantEventType::ConnectFailure,
         "failed_to_reopen_mqtt_audio_channel",
         "failed_to_reopen_mqtt_audio_channel", false});
    return false;
  }

  return true;
}

void connect_task(void *argument) {
  (void)argument;
  int64_t next_mqtt_connect_attempt_at_ms = 0;
  uint32_t mqtt_connect_backoff_ms = kServiceMqttReconnectBackoffMinMs;
  bool release_before_mqtt_attempt = true;
  int64_t next_udp_connect_attempt_at_ms = 0;
  uint32_t udp_connect_backoff_ms = kServiceUdpReconnectBackoffMinMs;
  std::string last_udp_endpoint;
  for (;;) {
    bool should_connect = false;
    bool should_connect_udp = false;
    bool afe_was_initialized = false;
    std::string udp_host;
    int udp_port = 0;

    std::shared_ptr<NetworkMqttSession> mqtt_client;
    std::shared_ptr<NetworkUdpChannel> udp_client;
    std::shared_ptr<NetworkUdpChannel> closing_udp;
    {
      std::lock_guard<std::mutex> lock(g_runtime.mutex);
      if (g_runtime.close_audio_channel_requested.exchange(false)) {
        if (g_runtime.transport.udp_channel != nullptr) {
          closing_udp = std::move(g_runtime.transport.udp_channel);
        }
      }
      should_connect = g_runtime.service_enabled &&
                       !g_runtime.service_suspended &&
                       session_connect_inflight_locked() &&
                       g_runtime.transport.mqtt_session == nullptr &&
                       !g_runtime.wake_word.afe_init_in_progress;
    }

    if (closing_udp != nullptr) {
      closing_udp->Disconnect();
      next_udp_connect_attempt_at_ms = 0;
      udp_connect_backoff_ms = kServiceUdpReconnectBackoffMinMs;
      last_udp_endpoint.clear();
    }

    if (should_connect) {
      bool still_should_connect = false;
      {
        std::lock_guard<std::mutex> lock(g_runtime.mutex);
        still_should_connect =
            g_runtime.service_enabled && !g_runtime.service_suspended &&
            session_connect_inflight_locked() &&
            g_runtime.transport.mqtt_session == nullptr &&
            !g_runtime.wake_word.afe_init_in_progress;
      }
      if (!still_should_connect) {
        vTaskDelay(pdMS_TO_TICKS(10));
        continue;
      }

      const int64_t current_ms = now_ms();
      if (next_mqtt_connect_attempt_at_ms > current_ms) {
        const int64_t wait_ms = next_mqtt_connect_attempt_at_ms - current_ms;
        vTaskDelay(pdMS_TO_TICKS(wait_ms > 250 ? 250 : wait_ms));
        continue;
      }

      std::string time_sync_error;
      if (!assistant_synchronize_network_clock(kSystemTimeSyncTimeoutMs,
                                               time_sync_error)) {
        next_mqtt_connect_attempt_at_ms =
            now_ms() + static_cast<int64_t>(mqtt_connect_backoff_ms);
        ESP_LOGW(kTag,
                 "service MQTT waiting for valid system time error=%s; retry in %" PRIu32 " ms",
                 time_sync_error.empty() ? "clock_sync_failed"
                                         : time_sync_error.c_str(),
                 mqtt_connect_backoff_ms);
        mqtt_connect_backoff_ms =
            std::min<uint32_t>(mqtt_connect_backoff_ms * 2U,
                               kServiceMqttReconnectBackoffMaxMs);
        vTaskDelay(pdMS_TO_TICKS(10));
        continue;
      }

      if (release_before_mqtt_attempt) {
        release_wake_frontend_for_transport();
        release_before_mqtt_attempt = false;
      }

      {
        std::lock_guard<std::mutex> lock(g_runtime.mutex);
        afe_was_initialized = g_runtime.wake_word.afe_initialized;

        NetworkMqttSessionConfig config = {};
        config.endpoint = g_runtime.transport.mqtt_endpoint;
        config.client_id = g_runtime.transport.mqtt_client_id;
        config.username = g_runtime.transport.mqtt_username;
        config.password = g_runtime.transport.mqtt_password;
        config.publish_topic = g_runtime.transport.mqtt_publish_topic;
        config.subscribe_topic = g_runtime.transport.mqtt_subscribe_topic;
        config.keep_alive_seconds = 60;
        config.timeout_ms = kConnectTimeoutMs;
        config.buffer_size = 2048;
        config.out_buffer_size = 2048;
        config.task_stack_size = kServiceMqttTaskStackSize;
        g_runtime.transport.mqtt_session =
            network_open_mqtt_session(std::move(config));
        if (g_runtime.transport.mqtt_session == nullptr) {
          (void)post_assistant_event_locked(
              {AssistantEventType::ConnectFailure,
               "failed_to_init_service_mqtt", {}, false});
          next_mqtt_connect_attempt_at_ms =
              now_ms() + static_cast<int64_t>(mqtt_connect_backoff_ms);
          ESP_LOGW(kTag, "service MQTT init failed; retry in %" PRIu32 " ms",
                   mqtt_connect_backoff_ms);
          release_before_mqtt_attempt = afe_was_initialized;
          mqtt_connect_backoff_ms =
              std::min<uint32_t>(mqtt_connect_backoff_ms * 2U,
                                 kServiceMqttReconnectBackoffMaxMs);
          continue;
        }
        bind_service_mqtt_callbacks_locked();
        g_runtime.transport.connect_started_at_ms = now_ms();
        // Start() can overlap a cloud-maintenance suspension. Keep an owning
        // reference until the complete connect/subscribe attempt finishes so
        // taking the runtime-owned reference cannot destroy the session under
        // this task.
        mqtt_client = g_runtime.transport.mqtt_session;
      }
    }


    if (mqtt_client != nullptr) {
      if (!mqtt_client->Start()) {
        const int mqtt_error = mqtt_client->GetLastError();
        std::shared_ptr<NetworkMqttSession> failed_mqtt;
        {
          std::lock_guard<std::mutex> lock(g_runtime.mutex);
          if (g_runtime.transport.mqtt_session.get() == mqtt_client.get()) {
            (void)post_assistant_event_locked(
                {AssistantEventType::ConnectFailure,
                 "failed_to_connect_service_mqtt", {}, false});
            failed_mqtt = take_service_mqtt_locked();
          }
        }
        failed_mqtt.reset();
        next_mqtt_connect_attempt_at_ms =
            now_ms() + static_cast<int64_t>(mqtt_connect_backoff_ms);
        ESP_LOGW(kTag,
                 "service MQTT start failed error=%d; retry in %" PRIu32 " ms",
                 mqtt_error, mqtt_connect_backoff_ms);
        release_before_mqtt_attempt = afe_was_initialized;
        mqtt_connect_backoff_ms =
            std::min<uint32_t>(mqtt_connect_backoff_ms * 2U,
                               kServiceMqttReconnectBackoffMaxMs);
        vTaskDelay(pdMS_TO_TICKS(10));
        continue;
      }
      next_mqtt_connect_attempt_at_ms = 0;
      mqtt_connect_backoff_ms = kServiceMqttReconnectBackoffMinMs;
      const bool has_subscribe_topic =
          !g_runtime.transport.mqtt_subscribe_topic.empty() &&
          g_runtime.transport.mqtt_subscribe_topic != "null";
      if (has_subscribe_topic && !mqtt_client->Subscribe()) {
        std::lock_guard<std::mutex> lock(g_runtime.mutex);
        g_runtime.diagnostics.last_transport_error =
            "service_mqtt_subscribe_failed";
        g_runtime.diagnostics.last_error =
            g_runtime.diagnostics.last_transport_error;
      }
    }

    {
      std::lock_guard<std::mutex> lock(g_runtime.mutex);
      if (g_runtime.service_enabled && !g_runtime.service_suspended &&
          g_runtime.transport.connected &&
          !session_audio_path_ready_locked() &&
          g_runtime.transport.udp_channel != nullptr &&
          g_runtime.transport.mqtt_udp_channel.ready()) {
        should_connect_udp = true;
        udp_client = g_runtime.transport.udp_channel;
        udp_host = g_runtime.transport.mqtt_udp_channel.server();
        udp_port = g_runtime.transport.mqtt_udp_channel.port();
      }
    }

    if (should_connect_udp && udp_client != nullptr) {
      const std::string udp_endpoint =
          udp_host + ":" + std::to_string(udp_port);
      if (udp_endpoint != last_udp_endpoint) {
        last_udp_endpoint = udp_endpoint;
        next_udp_connect_attempt_at_ms = 0;
        udp_connect_backoff_ms = kServiceUdpReconnectBackoffMinMs;
      }

      const int64_t current_ms = now_ms();
      if (next_udp_connect_attempt_at_ms > current_ms) {
        const int64_t wait_ms = next_udp_connect_attempt_at_ms - current_ms;
        vTaskDelay(pdMS_TO_TICKS(wait_ms > 250 ? 250 : wait_ms));
        continue;
      }

      ESP_LOGI(kTag, "service UDP connect begin remote=%s %s",
               udp_endpoint.c_str(), heap_snapshot_text().c_str());
      if (!udp_client->Connect(udp_host, udp_port)) {
        const int udp_error = udp_client->GetLastError();
        std::shared_ptr<NetworkUdpChannel> failed_udp;
        {
          std::lock_guard<std::mutex> lock(g_runtime.mutex);
          if (g_runtime.transport.udp_channel.get() == udp_client.get()) {
            failed_udp = std::move(g_runtime.transport.udp_channel);
            g_runtime.transport.mqtt_udp_channel.reset();
            g_runtime.transport.session_id.clear();
            set_session_has_announcement_locked(false);
          }
          const std::string detail =
              "service_udp_connect_failed errno=" + std::to_string(udp_error);
          (void)post_assistant_event_locked(
              {AssistantEventType::ConnectFailure,
               "service_udp_connect_failed", detail, true});
        }
        next_udp_connect_attempt_at_ms =
            now_ms() + static_cast<int64_t>(udp_connect_backoff_ms);
        ESP_LOGW(kTag,
                 "service UDP connect failed error=%d; retry in %" PRIu32
                 " ms %s",
                 udp_error, udp_connect_backoff_ms,
                 heap_snapshot_text().c_str());
        udp_connect_backoff_ms =
            std::min<uint32_t>(udp_connect_backoff_ms * 2U,
                               kServiceUdpReconnectBackoffMaxMs);
        if (failed_udp != nullptr) {
          failed_udp->Disconnect();
        }
      } else {
        next_udp_connect_attempt_at_ms = 0;
        udp_connect_backoff_ms = kServiceUdpReconnectBackoffMinMs;
        ESP_LOGI(kTag, "service UDP connect ok remote=%s %s",
                 udp_endpoint.c_str(), heap_snapshot_text().c_str());
        std::lock_guard<std::mutex> lock(g_runtime.mutex);
        if (g_runtime.transport.udp_channel.get() == udp_client.get()) {
          (void)post_assistant_event_locked(
              {AssistantEventType::AudioChannelReady, {}, {}, false});
        }
      }
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

bool send_service_text_locked(const std::string &message) {
  (void)message;
  if (!session_control_channel_connected_locked()) {
    g_runtime.diagnostics.last_error = "service_mqtt_not_open";
    return false;
  }
  return true;
}

bool send_service_text(const std::string &message) {
  std::shared_ptr<NetworkMqttSession> mqtt_session;
  {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    if (!send_service_text_locked(message)) {
      ESP_LOGW(kTag,
               "[DEBUG-MQTT-LINK] service text rejected before publish "
               "size=%u %s",
               static_cast<unsigned>(message.size()),
               heap_snapshot_text().c_str());
      return false;
    }
    mqtt_session = g_runtime.transport.mqtt_session;
  }

  bool ok = false;
  std::string transport_error;
  ESP_LOGD(kTag, "[DEBUG-MQTT-LINK] service text publish begin size=%u %s",
           static_cast<unsigned>(message.size()), heap_snapshot_text().c_str());
  ok = mqtt_session != nullptr && mqtt_session->Publish(message);
  if (!ok) {
    transport_error = "service_mqtt_publish_failed";
  }
  ESP_LOGD(kTag,
           "[DEBUG-MQTT-LINK] service text publish end size=%u ok=%d %s",
           static_cast<unsigned>(message.size()), ok ? 1 : 0,
           heap_snapshot_text().c_str());

  if (!ok) {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    g_runtime.diagnostics.last_transport_error = transport_error;
    g_runtime.diagnostics.last_error = g_runtime.diagnostics.last_transport_error;
    (void)post_assistant_event_locked(
        {AssistantEventType::TransportDisconnected, "UDP 音频通道已断开", {}, false});
  }
  return ok;
}

bool send_service_binary_locked(const std::vector<uint8_t> &message) {
  (void)message;
  if (!session_audio_path_ready_locked()) {
    g_runtime.diagnostics.last_error = "service_udp_not_open";
    return false;
  }
  return true;
}

bool send_service_binary(const std::vector<uint8_t> &message) {
  std::shared_ptr<NetworkUdpChannel> udp_channel;
  std::string datagram;
  uint32_t timestamp_ms = 0;
  {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    if (!send_service_binary_locked(message)) {
      return false;
    }
    udp_channel = g_runtime.transport.udp_channel;
    timestamp_ms = 0;
    if (!g_runtime.transport.mqtt_udp_channel.encrypt_payload(
            message.data(), message.size(), timestamp_ms, datagram)) {
      g_runtime.diagnostics.last_transport_error =
          "service_udp_encrypt_failed:" +
          std::to_string(g_runtime.transport.mqtt_udp_channel.last_crypto_error());
      g_runtime.diagnostics.last_error = g_runtime.diagnostics.last_transport_error;
      return false;
    }
  }

  bool ok = false;
  std::string transport_error;
  ok = udp_channel != nullptr && udp_channel->Send(datagram) > 0;
  if (!ok) {
    transport_error = "service_udp_send_audio_failed";
  }

  std::lock_guard<std::mutex> lock(g_runtime.mutex);
  if (!ok) {
    g_runtime.diagnostics.last_transport_error = transport_error;
    g_runtime.diagnostics.last_error = g_runtime.diagnostics.last_transport_error;
    return false;
  }
  g_runtime.sent_audio_packets += 1;
  return true;
}

} // namespace lunaredu::pet::conversation::internal
