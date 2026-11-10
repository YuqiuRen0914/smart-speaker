#include "interaction/transport/network_xiaozhi_protocol.h"

#include <ArduinoJson.h>

namespace {

std::string json_string(const std::string &value) {
  std::string output = "\"";
  output.reserve(value.size() + 2);
  for (char ch : value) {
    switch (ch) {
    case '\\':
      output += "\\\\";
      break;
    case '"':
      output += "\\\"";
      break;
    case '\n':
      output += "\\n";
      break;
    case '\r':
      output += "\\r";
      break;
    case '\t':
      output += "\\t";
      break;
    default:
      output.push_back(ch);
      break;
    }
  }
  output.push_back('"');
  return output;
}

} // namespace

std::string xiaozhi_build_mqtt_hello_message() {
  return std::string(
      "{\"type\":\"hello\",\"version\":3,"
      "\"features\":{\"mcp\":true},\"transport\":\"udp\","
      "\"audio_params\":{\"format\":\"opus\",\"sample_rate\":16000,"
      "\"channels\":1,\"frame_duration\":60}}");
}

std::string xiaozhi_build_listen_start_message(const std::string &session_id,
                                               const std::string &mode) {
  return "{\"session_id\":" + json_string(session_id) +
         ",\"type\":\"listen\",\"state\":\"start\",\"mode\":" +
         json_string(mode) + "}";
}

std::string xiaozhi_build_listen_detect_message(const std::string &session_id,
                                                const std::string &text) {
  return "{\"session_id\":" + json_string(session_id) +
         ",\"type\":\"listen\",\"state\":\"detect\",\"mode\":\"manual\",\"text\":" +
         json_string(text) + "}";
}

std::string xiaozhi_build_listen_stop_message(const std::string &session_id) {
  return "{\"session_id\":" + json_string(session_id) +
         ",\"type\":\"listen\",\"state\":\"stop\"}";
}

std::string xiaozhi_build_abort_message(const std::string &session_id) {
  return "{\"session_id\":" + json_string(session_id) +
         ",\"type\":\"abort\"}";
}

std::string xiaozhi_summarize_message(const std::string &message) {
  JsonDocument doc;
  if (deserializeJson(doc, message) != DeserializationError::Ok) {
    if (message.size() > 180) {
      return message.substr(0, 180) + "...";
    }
    return message;
  }
  const char *type = doc["type"] | "";
  const char *state = doc["state"] | "";
  const char *text = doc["text"] | "";
  const char *method = doc["method"] | "";
  std::string summary = *type != '\0' ? type : message;
  if (*state != '\0') {
    summary += "/";
    summary += state;
  }
  if (*text != '\0') {
    summary += ": ";
    summary += text;
  } else if (*method != '\0') {
    summary += ": ";
    summary += method;
  }
  if (summary.size() > 180) {
    summary.resize(180);
    summary += "...";
  }
  return summary;
}

bool xiaozhi_parse_mqtt_server_hello(const std::string &message,
                                     XiaozhiUdpHelloConfig &config) {
  config = XiaozhiUdpHelloConfig{};
  JsonDocument doc;
  if (deserializeJson(doc, message) != DeserializationError::Ok) {
    config.error = "invalid_mqtt_hello_json";
    return false;
  }

  const std::string type = doc["type"] | "";
  const std::string transport = doc["transport"] | "";
  if (type != "hello") {
    config.error = "mqtt_message_not_hello";
    return false;
  }
  if (transport != "udp") {
    config.error = "mqtt_hello_transport_not_udp";
    return false;
  }

  config.session_id = doc["session_id"] | "";
  if (config.session_id.empty()) {
    config.error = "mqtt_hello_missing_session_id";
    return false;
  }

  JsonObject audio_params = doc["audio_params"].as<JsonObject>();
  config.sample_rate = audio_params["sample_rate"] | 16000;
  config.channels = audio_params["channels"] | 1;
  config.frame_duration_ms = audio_params["frame_duration"] | 60;

  JsonObject udp = doc["udp"].as<JsonObject>();
  config.server = udp["server"] | "";
  config.port = udp["port"] | 0;
  config.key = udp["key"] | "";
  config.nonce = udp["nonce"] | "";
  if (config.server.empty() || config.port <= 0 || config.key.empty() ||
      config.nonce.empty()) {
    config.error = "mqtt_hello_missing_udp_config";
    return false;
  }

  config.ok = true;
  return true;
}
