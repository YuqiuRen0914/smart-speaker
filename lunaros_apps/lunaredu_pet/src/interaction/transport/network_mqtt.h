#pragma once

#include <functional>
#include <memory>
#include <string>

struct NetworkMqttSessionConfig {
  std::string endpoint;
  std::string client_id;
  std::string username;
  std::string password;
  std::string publish_topic;
  std::string subscribe_topic;
  int keep_alive_seconds = 240;
  int timeout_ms = 4000;
  int buffer_size = 2048;
  int out_buffer_size = 0;
  int task_stack_size = 4096;
};

class NetworkMqttSession {
 public:
  virtual ~NetworkMqttSession() = default;

  virtual bool Configured() const = 0;
  virtual bool Start() = 0;
  virtual void Disconnect() = 0;
  virtual bool Publish(const std::string &payload, int qos = 0) = 0;
  virtual bool Subscribe(int qos = 0) = 0;
  virtual bool IsConnected() = 0;

  virtual void OnConnected(std::function<void()> callback) = 0;
  virtual void OnDisconnected(std::function<void()> callback) = 0;
  virtual void OnMessage(
      std::function<void(const std::string &topic, const std::string &payload)>
          callback) = 0;
  virtual void OnError(std::function<void(const std::string &error)> callback) =
      0;

  virtual int GetLastError() = 0;
};

std::unique_ptr<NetworkMqttSession>
network_open_mqtt_session(NetworkMqttSessionConfig config);

