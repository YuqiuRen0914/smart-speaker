#include "interaction/transport/mqtt_client_support.h"

#include <esp_crt_bundle.h>
#include <esp_heap_caps.h>
#include <esp_log.h>

namespace {

constexpr const char *kLogTag = "mqtt_support";
constexpr int kSecureMqttPort = 8883;

void log_mqtt_heap(const char *phase, const MqttClientStartConfig &config,
                   esp_err_t result = ESP_OK) {
  ESP_LOGI(kLogTag,
           "%s result=%s stack=%d buffer=%d heap_free=%u heap_min=%u sram_free=%u sram_largest=%u",
           phase, esp_err_to_name(result), config.task_stack_size,
           config.buffer_size, static_cast<unsigned>(esp_get_free_heap_size()),
           static_cast<unsigned>(esp_get_minimum_free_heap_size()),
           static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
           static_cast<unsigned>(
               heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)));
}

} // namespace

esp_mqtt_client_handle_t start_mqtt_client(
    const MqttClientStartConfig &config, esp_event_handler_t event_handler,
    void *event_handler_arg, int *last_error_state) {
  log_mqtt_heap("mqtt init begin", config);
  esp_mqtt_client_config_t mqtt_config = {};
  mqtt_config.task.stack_size = config.task_stack_size;
  if (!config.broker_uri.empty()) {
    mqtt_config.broker.address.uri = config.broker_uri.c_str();
    if (config.broker_uri.rfind("wss://", 0) == 0 ||
        config.broker_uri.rfind("mqtts://", 0) == 0) {
      mqtt_config.broker.verification.crt_bundle_attach = esp_crt_bundle_attach;
    }
  } else {
    mqtt_config.broker.address.hostname = config.broker_address.c_str();
    mqtt_config.broker.address.port = config.broker_port;
    if (config.broker_port == kSecureMqttPort) {
      mqtt_config.broker.address.transport = MQTT_TRANSPORT_OVER_SSL;
      mqtt_config.broker.verification.crt_bundle_attach = esp_crt_bundle_attach;
    } else {
      mqtt_config.broker.address.transport = MQTT_TRANSPORT_OVER_TCP;
    }
  }
  mqtt_config.credentials.client_id = config.client_id.c_str();
  mqtt_config.credentials.username = config.username.c_str();
  mqtt_config.credentials.authentication.password = config.password.c_str();
  mqtt_config.session.keepalive = config.keep_alive_seconds;
  mqtt_config.network.timeout_ms = config.timeout_ms;
  mqtt_config.buffer.size = config.buffer_size;
  mqtt_config.buffer.out_size = config.out_buffer_size > 0
                                    ? config.out_buffer_size
                                    : config.buffer_size;

  if (!config.last_will_topic.empty()) {
    mqtt_config.session.last_will.topic = config.last_will_topic.c_str();
    mqtt_config.session.last_will.msg = config.last_will_payload.c_str();
    mqtt_config.session.last_will.qos = 0;
    mqtt_config.session.last_will.retain = 0;
  }

  esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_config);
  if (client == nullptr) {
    if (last_error_state != nullptr) {
      *last_error_state = -1;
    }
    log_mqtt_heap("mqtt init failed", config, ESP_ERR_NO_MEM);
    return nullptr;
  }

  const esp_err_t register_error = esp_mqtt_client_register_event(
      client, MQTT_EVENT_ANY, event_handler, event_handler_arg);
  if (register_error != ESP_OK) {
    if (last_error_state != nullptr) {
      *last_error_state = static_cast<int>(register_error);
    }
    log_mqtt_heap("mqtt register failed", config, register_error);
    esp_mqtt_client_destroy(client);
    return nullptr;
  }

  log_mqtt_heap("mqtt start begin", config);
  const esp_err_t start_error = esp_mqtt_client_start(client);
  if (start_error != ESP_OK) {
    if (last_error_state != nullptr) {
      *last_error_state = static_cast<int>(start_error);
    }
    log_mqtt_heap("mqtt start failed", config, start_error);
    esp_mqtt_client_destroy(client);
    return nullptr;
  }

  log_mqtt_heap("mqtt start ok", config);
  if (last_error_state != nullptr) {
    *last_error_state = 0;
  }
  return client;
}

void destroy_mqtt_client(esp_mqtt_client_handle_t client, bool started) {
  if (client == nullptr) {
    return;
  }

  if (started) {
    esp_mqtt_client_stop(client);
  }
  esp_mqtt_client_destroy(client);
}
