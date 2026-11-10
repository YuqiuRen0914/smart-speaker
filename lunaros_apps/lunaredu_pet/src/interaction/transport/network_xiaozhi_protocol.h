#pragma once

#include <string>

struct XiaozhiUdpHelloConfig {
  bool ok = false;
  std::string session_id;
  std::string server;
  int port = 0;
  std::string key;
  std::string nonce;
  int sample_rate = 16000;
  int channels = 1;
  int frame_duration_ms = 60;
  std::string error;
};

std::string xiaozhi_build_mqtt_hello_message();
std::string xiaozhi_build_listen_start_message(const std::string &session_id,
                                               const std::string &mode);
std::string xiaozhi_build_listen_detect_message(const std::string &session_id,
                                                const std::string &text);
std::string xiaozhi_build_listen_stop_message(const std::string &session_id);
std::string xiaozhi_build_abort_message(const std::string &session_id);
std::string xiaozhi_summarize_message(const std::string &message);

bool xiaozhi_parse_mqtt_server_hello(const std::string &message,
                                     XiaozhiUdpHelloConfig &config);
