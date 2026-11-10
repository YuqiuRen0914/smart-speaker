#pragma once

#include <cstdint>
#include <string>

struct XiaozhiActivationRequest {
  std::string ota_url;
  std::string board_name;
  std::string device_id;
  std::string client_id;
  uint32_t activation_timeout_ms = 30000;
};

struct XiaozhiActivationResult {
  bool ok = false;
  std::string activation_message;
  std::string activation_code;
  std::string activation_challenge;
  uint32_t activation_timeout_ms = 0;
  bool activation_pending = false;
  bool has_mqtt = false;
  std::string mqtt_endpoint;
  std::string mqtt_client_id;
  std::string mqtt_username;
  std::string mqtt_password;
  std::string mqtt_publish_topic;
  std::string mqtt_subscribe_topic;
  bool has_server_time = false;
  int64_t server_time_ms = 0;
  int timezone_offset_minutes = 480;
  std::string error;
  std::string transport_error;
};

bool xiaozhi_activate_device(const XiaozhiActivationRequest &request,
                             XiaozhiActivationResult &result);
