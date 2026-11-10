#include "interaction/transport/network_mqtt.h"

#include "interaction/transport/mqtt_client_support.h"

#include <esp_event.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <mqtt_client.h>
#include <mbedtls/x509_crt.h>

#include <atomic>
#include <cstdlib>
#include <mutex>
#include <string>
#include <utility>

namespace {

constexpr const char *kTag = "network_mqtt";
constexpr int kMqttConnectTimeoutMs = 10000;
constexpr EventBits_t kMqttConnectedEvent = BIT1;
constexpr EventBits_t kMqttDisconnectedEvent = BIT2;
constexpr EventBits_t kMqttErrorEvent = BIT3;

void log_mqtt_publish_heap(const char *phase, size_t payload_size,
                           int result, int last_error) {
#if CONFIG_LOG_MAXIMUM_LEVEL >= 4
  ESP_LOGD(
      kTag,
      "[DEBUG-MQTT-LINK] %s payload=%u result=%d last_error=%d heap_free=%u "
      "heap_min=%u internal_free=%u internal_largest=%u psram_free=%u "
      "psram_largest=%u",
      phase, static_cast<unsigned>(payload_size), result, last_error,
      static_cast<unsigned>(esp_get_free_heap_size()),
      static_cast<unsigned>(esp_get_minimum_free_heap_size()),
      static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
      static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
      static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
      static_cast<unsigned>(
          heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)));
#else
  (void)phase;
  (void)payload_size;
  (void)result;
  (void)last_error;
#endif
}

std::string format_certificate_verify_flags(int flags) {
  if (flags == 0) {
    return "none";
  }
  char buffer[320] = {};
  const int written = mbedtls_x509_crt_verify_info(
      buffer, sizeof(buffer), "", static_cast<uint32_t>(flags));
  return written > 0 ? std::string(buffer) : std::string("unknown");
}

bool ParseEndpoint(const std::string &endpoint, std::string *broker_address,
                   int *broker_port) {
  if (broker_address == nullptr || broker_port == nullptr || endpoint.empty()) {
    return false;
  }

  if (endpoint.find("://") != std::string::npos) {
    return true;
  }

  *broker_port = 8883;
  const size_t pos = endpoint.find(':');
  if (pos == std::string::npos) {
    *broker_address = endpoint;
    return true;
  }

  *broker_address = endpoint.substr(0, pos);
  const std::string port_part = endpoint.substr(pos + 1);
  if (broker_address->empty() || port_part.empty()) {
    return false;
  }

  char *end = nullptr;
  const long parsed_port = std::strtol(port_part.c_str(), &end, 10);
  if (end == nullptr || *end != '\0' || parsed_port <= 0 ||
      parsed_port > 65535) {
    return false;
  }

  *broker_port = static_cast<int>(parsed_port);
  return true;
}

class NetworkOwnedMqttSession final : public NetworkMqttSession {
 public:
  explicit NetworkOwnedMqttSession(NetworkMqttSessionConfig config)
      : config_(std::move(config)) {
    event_group_handle_ = xEventGroupCreate();
  }

  ~NetworkOwnedMqttSession() override {
    Disconnect();
    if (event_group_handle_ != nullptr) {
      vEventGroupDelete(event_group_handle_);
    }
  }

  bool Configured() const override { return !config_.endpoint.empty(); }

  bool Start() override {
    // A cloud-maintenance transition can concurrently take ownership of this
    // session and call Disconnect().  Serialize the whole start/stop
    // lifecycle so the event group (which Connect() waits on) cannot be
    // destroyed while the connect task is still waiting on it.
    std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
    if (!Configured()) {
      last_error_ = -1;
      ESP_LOGW(kTag, "MQTT 终端地址未配置");
      return false;
    }

    std::string broker_address;
    int broker_port = 8883;
    if (!ParseEndpoint(config_.endpoint, &broker_address, &broker_port)) {
      last_error_ = -2;
      ESP_LOGE(kTag, "MQTT 终端地址无效：%s", config_.endpoint.c_str());
      return false;
    }

    ESP_LOGD(kTag, "准备连接 MQTT 终端：%s", config_.endpoint.c_str());
    const bool ok = config_.endpoint.find("://") != std::string::npos
                        ? ConnectUri(config_.endpoint)
                        : Connect(broker_address, broker_port);
    last_error_ = ok ? 0 : GetLastError();
    if (ok) {
      ESP_LOGD(kTag, "MQTT 终端连接成功");
    }
    return ok;
  }

  void Disconnect() override {
    std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
    disconnect_unlocked();
  }

 private:
  void disconnect_unlocked() {
    if (mqtt_client_handle_ != nullptr) {
      destroy_mqtt_client(mqtt_client_handle_, true);
      mqtt_client_handle_ = nullptr;
    }
    connected_.store(false);
    message_payload_.clear();
    xEventGroupClearBits(event_group_handle_,
                         kMqttConnectedEvent | kMqttDisconnectedEvent |
                             kMqttErrorEvent);
  }

 public:

  bool Publish(const std::string &payload, int qos = 0) override {
    // A retained session may still receive a final publish while another task
    // is suspending the service. Serialize access to the ESP MQTT handle with
    // Disconnect() so the handle cannot be destroyed during this operation.
    std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
    if (config_.publish_topic.empty()) {
      last_error_ = -3;
      log_mqtt_publish_heap("publish rejected: no topic", payload.size(), -1,
                            GetLastError());
      return false;
    }
    if (!connected_.load()) {
      last_error_ = -4;
      log_mqtt_publish_heap("publish rejected: disconnected", payload.size(),
                            -1, GetLastError());
      return false;
    }
    log_mqtt_publish_heap("publish before", payload.size(), 0,
                          GetLastError());
    const int msg_id = esp_mqtt_client_publish(mqtt_client_handle_,
                                               config_.publish_topic.c_str(),
                                               payload.data(), payload.size(),
                                               qos, 0);
    if (msg_id == -1) {
      last_error_ = -6;
    }
    log_mqtt_publish_heap("publish after", payload.size(), msg_id,
                          GetLastError());
    return msg_id != -1;
  }

  bool Subscribe(int qos = 0) override {
    std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
    if (config_.subscribe_topic.empty()) {
      last_error_ = -5;
      return false;
    }
    if (!connected_.load()) {
      last_error_ = -4;
      return false;
    }
    return esp_mqtt_client_subscribe_single(mqtt_client_handle_,
                                            config_.subscribe_topic.c_str(),
                                            qos) > 0;
  }

  bool IsConnected() override { return connected_.load(); }

  void OnConnected(std::function<void()> callback) override {
    on_connected_callback_ = std::move(callback);
  }

  void OnDisconnected(std::function<void()> callback) override {
    on_disconnected_callback_ = std::move(callback);
  }

  void OnMessage(
      std::function<void(const std::string &topic, const std::string &payload)>
          callback) override {
    on_message_callback_ = std::move(callback);
  }

  void OnError(
      std::function<void(const std::string &error)> callback) override {
    on_error_callback_ = std::move(callback);
  }

  int GetLastError() override {
    return last_error_.load(std::memory_order_acquire);
  }

 private:
  bool Connect(const std::string &broker_address, int broker_port) {
    if (mqtt_client_handle_ != nullptr) {
      disconnect_unlocked();
    }

    MqttClientStartConfig config{};
    config.broker_address = broker_address;
    config.broker_port = broker_port;
    config.client_id = config_.client_id;
    config.username = config_.username;
    config.password = config_.password;
    config.keep_alive_seconds = config_.keep_alive_seconds;
    config.timeout_ms = config_.timeout_ms;
    config.buffer_size = config_.buffer_size;
    config.out_buffer_size = config_.out_buffer_size;
    config.task_stack_size = config_.task_stack_size;

    int start_error = 0;
    mqtt_client_handle_ = start_mqtt_client(
        config,
        [](void *handler_args, esp_event_base_t base, int32_t event_id,
           void *event_data) {
          static_cast<NetworkOwnedMqttSession *>(handler_args)
              ->MqttEventCallback(base, event_id, event_data);
        },
        this, &start_error);
    last_error_.store(start_error, std::memory_order_release);
    if (mqtt_client_handle_ == nullptr) {
      return false;
    }

    const auto bits = xEventGroupWaitBits(
        event_group_handle_,
        kMqttConnectedEvent | kMqttDisconnectedEvent | kMqttErrorEvent, pdTRUE,
        pdFALSE, pdMS_TO_TICKS(kMqttConnectTimeoutMs));
    return (bits & kMqttConnectedEvent) != 0;
  }

  bool ConnectUri(const std::string &broker_uri) {
    if (mqtt_client_handle_ != nullptr) {
      disconnect_unlocked();
    }

    MqttClientStartConfig config{};
    config.broker_uri = broker_uri;
    config.client_id = config_.client_id;
    config.username = config_.username;
    config.password = config_.password;
    config.keep_alive_seconds = config_.keep_alive_seconds;
    config.timeout_ms = config_.timeout_ms;
    config.buffer_size = config_.buffer_size;
    config.out_buffer_size = config_.out_buffer_size;
    config.task_stack_size = config_.task_stack_size;

    int start_error = 0;
    mqtt_client_handle_ = start_mqtt_client(
        config,
        [](void *handler_args, esp_event_base_t base, int32_t event_id,
           void *event_data) {
          static_cast<NetworkOwnedMqttSession *>(handler_args)
              ->MqttEventCallback(base, event_id, event_data);
        },
        this, &start_error);
    last_error_.store(start_error, std::memory_order_release);
    if (mqtt_client_handle_ == nullptr) {
      return false;
    }

    const auto bits = xEventGroupWaitBits(
        event_group_handle_,
        kMqttConnectedEvent | kMqttDisconnectedEvent | kMqttErrorEvent, pdTRUE,
        pdFALSE, pdMS_TO_TICKS(kMqttConnectTimeoutMs));
    return (bits & kMqttConnectedEvent) != 0;
  }

  void MqttEventCallback(esp_event_base_t base, int32_t event_id,
                         void *event_data) {
    (void)base;
    auto *event = static_cast<esp_mqtt_event_t *>(event_data);
    switch (event_id) {
    case MQTT_EVENT_CONNECTED:
      if (!connected_.exchange(true)) {
        if (on_connected_callback_) {
          on_connected_callback_();
        }
      }
      xEventGroupSetBits(event_group_handle_, kMqttConnectedEvent);
      break;
    case MQTT_EVENT_DISCONNECTED:
      if (connected_.exchange(false)) {
        if (on_disconnected_callback_) {
          on_disconnected_callback_();
        }
      }
      xEventGroupSetBits(event_group_handle_, kMqttDisconnectedEvent);
      break;
    case MQTT_EVENT_DATA: {
      const std::string topic(event->topic, event->topic_len);
      const std::string payload(event->data, event->data_len);
      if (event->data_len == event->total_data_len) {
        if (on_message_callback_) {
          on_message_callback_(topic, payload);
        }
      } else {
        message_payload_.append(payload);
        if (message_payload_.size() >= event->total_data_len &&
            on_message_callback_) {
          on_message_callback_(topic, message_payload_);
          message_payload_.clear();
        }
      }
      break;
    }
    case MQTT_EVENT_ERROR: {
      if (event->error_handle == nullptr) {
        last_error_ = -7;
        ESP_LOGE(kTag, "[DEBUG-MQTT-LINK] MQTT_EVENT_ERROR without details");
        xEventGroupSetBits(event_group_handle_, kMqttErrorEvent);
        break;
      }
      last_error_ = event->error_handle->esp_tls_last_esp_err;
      xEventGroupSetBits(event_group_handle_, kMqttErrorEvent);
      const char *error_name =
          esp_err_to_name(event->error_handle->esp_tls_last_esp_err);
      const int verify_flags =
          event->error_handle->esp_tls_cert_verify_flags;
      const std::string verify_info =
          format_certificate_verify_flags(verify_flags);
      ESP_LOGE(kTag,
                "[DEBUG-MQTT-LINK] MQTT 会话错误：%s tls_err=%d "
                "tls_stack_err=%d cert_verify_flags=0x%x cert_verify=%s "
                "sock_errno=%d heap_free=%u internal_free=%u "
                "internal_largest=%u psram_free=%u psram_largest=%u",
                error_name, event->error_handle->esp_tls_last_esp_err,
                event->error_handle->esp_tls_stack_err,
                static_cast<unsigned>(verify_flags), verify_info.c_str(),
                event->error_handle->esp_transport_sock_errno,
               static_cast<unsigned>(esp_get_free_heap_size()),
               static_cast<unsigned>(
                   heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
               static_cast<unsigned>(
                   heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
               static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
               static_cast<unsigned>(
                   heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)));
      if (on_error_callback_) {
        std::string detail = error_name ? error_name : "MQTT error";
        if (verify_flags != 0) {
          detail += ": ";
          detail += verify_info;
        }
        on_error_callback_(detail);
      }
      break;
    }
    default:
      break;
    }
  }

  NetworkMqttSessionConfig config_;
  std::mutex lifecycle_mutex_;
  std::atomic<bool> connected_{false};
  EventGroupHandle_t event_group_handle_ = nullptr;
  std::string message_payload_;
  esp_mqtt_client_handle_t mqtt_client_handle_ = nullptr;
  std::atomic<int> last_error_{0};
  std::function<void(const std::string &topic, const std::string &payload)>
      on_message_callback_;
  std::function<void()> on_connected_callback_;
  std::function<void()> on_disconnected_callback_;
  std::function<void(const std::string &error)> on_error_callback_;
};

} // namespace

std::unique_ptr<NetworkMqttSession>
network_open_mqtt_session(NetworkMqttSessionConfig config) {
  return std::make_unique<NetworkOwnedMqttSession>(std::move(config));
}
