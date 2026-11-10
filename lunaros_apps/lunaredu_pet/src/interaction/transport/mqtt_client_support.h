#pragma once

#include <esp_event.h>
#include <mqtt_client.h>

#include <string>

struct MqttClientStartConfig {
  std::string broker_uri;
  std::string broker_address;
  int broker_port = 1883;
  std::string client_id;
  std::string username;
  std::string password;
  int keep_alive_seconds = 30;
  int timeout_ms = 4000;
  int buffer_size = 2048;
  // Keep the output packet buffer independently tunable.  The MQTT component
  // allocates both buffers in external RAM in this product, but avoiding a
  // second oversized default reduces pressure during TLS reconnects.
  int out_buffer_size = 0;
  int task_stack_size = 4096;
  std::string last_will_topic;
  std::string last_will_payload;
};

esp_mqtt_client_handle_t start_mqtt_client(
    const MqttClientStartConfig &config, esp_event_handler_t event_handler,
    void *event_handler_arg, int *last_error_state);

void destroy_mqtt_client(esp_mqtt_client_handle_t client, bool started);
