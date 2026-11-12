// XiaoZhi transport retained from this repository baseline.
#include "interaction/transport/assistant_activation_client.h"
#include "interaction/transport/assistant_provider.h"

#include <ArduinoJson.h>
#include <esp_err.h>

#include "lunaredu_lunaros/persistence/system_info.h"
#include "interaction/transport/assistant_transport.h"

namespace {

std::string format_esp_error(esp_err_t err) {
  if (err == ESP_OK) {
    return "ESP_OK";
  }
  char name[32] = {};
  esp_err_to_name_r(err, name, sizeof(name));
  return std::string(name);
}

std::string build_activation_payload(const XiaozhiActivationRequest &request) {
  static JsonDocument doc;
  doc.clear();
  doc["version"] = 2;
  doc["language"] = "zh-CN";
  doc["uuid"] = request.client_id;
  doc["flash_size"] = lunaredu::system::SystemInfo::GetFlashSize();
  doc["minimum_free_heap_size"] =
      lunaredu::system::SystemInfo::GetMinimumFreeHeapSize();
  doc["mac_address"] = request.device_id;
  doc["chip_model_name"] = lunaredu::system::SystemInfo::GetChipModelName();
  JsonObject board = doc["board"].to<JsonObject>();
  board["type"] = request.board_name;
  board["name"] = request.board_name;
  board["mac"] = request.device_id;
  JsonObject display = doc["display"].to<JsonObject>();
  display["monochrome"] = false;
  display["width"] = 240;
  display["height"] = 240;
  std::string body;
  serializeJson(doc, body);
  return body;
}

bool parse_activation_response(const std::string &body,
                               XiaozhiActivationResult &result) {
  static JsonDocument doc;
  doc.clear();
  if (deserializeJson(doc, body) != DeserializationError::Ok) {
    result.error = "invalid_activation_json";
    return false;
  }

  JsonObject activation = doc["activation"].as<JsonObject>();
  JsonObject mqtt = doc["mqtt"].as<JsonObject>();
  JsonObject server_time = doc["server_time"].as<JsonObject>();
  result.ok = true;
  result.activation_message = activation["message"] | "";
  result.activation_code = activation["code"] | "";
  result.activation_challenge = activation["challenge"] | "";
  result.activation_timeout_ms = activation["timeout_ms"] | 30000;
  result.activation_pending = false;
  result.mqtt_endpoint = mqtt["endpoint"] | "";
  result.has_mqtt = !result.mqtt_endpoint.empty();
  result.mqtt_client_id = mqtt["client_id"] | "";
  result.mqtt_username = mqtt["username"] | "";
  result.mqtt_password = mqtt["password"] | "";
  result.mqtt_publish_topic = mqtt["publish_topic"] | "";
  result.mqtt_subscribe_topic = mqtt["subscribe_topic"] | "";
  if (!server_time["timestamp"].isNull()) {
    result.has_server_time = true;
    result.server_time_ms = server_time["timestamp"].as<int64_t>();
    result.timezone_offset_minutes =
        server_time["timezone_offset"] | result.timezone_offset_minutes;
  }
  return true;
}

bool http_post_json(const XiaozhiActivationRequest &request,
                    const char *url,
                    const std::string &body,
                    std::string &response_body,
                    int &status_code,
                    std::string &transport_error) {
  lunaredu::lunaros::NetworkFetchRequest fetch_request{};
  fetch_request.maximum_response_bytes = 16U * 1024U;
  fetch_request.method = "POST";
  fetch_request.url = url == nullptr ? std::string() : std::string(url);
  fetch_request.body = body;
  fetch_request.timeout_ms = static_cast<int>(request.activation_timeout_ms);
  fetch_request.headers.push_back({"Activation-Version", "1"});
  fetch_request.headers.push_back({"Device-Id", request.device_id});
  fetch_request.headers.push_back({"Client-Id", request.client_id});
  fetch_request.headers.push_back({"User-Agent",
                                   "bread-compact-wifi-s3cam/pc-style-runtime"});
  fetch_request.headers.push_back({"Accept-Language", "zh-CN"});
  fetch_request.headers.push_back({"Content-Type", "application/json"});
#if ASSISTANT_USE_MAC
  fetch_request.transport_security = lunaredu::lunaros::NetworkTransportSecurity::AllowPlaintextLocal;
  if (std::string(CONFIG_LUNAREDU_ASSISTANT_MAC_TOKEN).size() < 32) {
    transport_error = "mac_bootstrap_token_missing";
    return false;
  }
  fetch_request.headers.push_back({"X-Voice-Token", CONFIG_LUNAREDU_ASSISTANT_MAC_TOKEN});
#endif

  lunaredu::lunaros::NetworkResponse response;
  if (!lunaredu::lunaros::network_fetch_resource(std::move(fetch_request),
                                                  &response)) {
    status_code = response.status_code;
    response_body = std::move(response.body);
    transport_error = response.error_detail.empty()
                          ? format_esp_error(
                                static_cast<esp_err_t>(response.last_error))
                          : response.error_detail;
    return false;
  }

  status_code = response.status_code;
  response_body = std::move(response.body);
  transport_error = std::move(response.error_detail);
  return true;
}

std::string activation_activate_url(const XiaozhiActivationRequest &request) {
  std::string base = request.ota_url;
  if (base.empty()) {
    return std::string();
  }
  if (base.back() != '/') {
    base.push_back('/');
  }
  base += "activate";
  return base;
}

bool fetch_activation(const XiaozhiActivationRequest &request,
                      XiaozhiActivationResult &result) {
  std::string response_body;
  int status_code = 0;
  std::string transport_error;
  const std::string payload = build_activation_payload(request);
  if (!http_post_json(request, request.ota_url.c_str(), payload, response_body,
                      status_code, transport_error)) {
    result.error = "activation_request_failed";
    result.transport_error = std::move(transport_error);
    return false;
  }
  if (status_code != 200) {
    result.error = "activation_bad_status_" + std::to_string(status_code);
    result.transport_error = std::move(transport_error);
    return false;
  }
  return parse_activation_response(response_body, result);
}

} // namespace

bool xiaozhi_activate_device(const XiaozhiActivationRequest &request,
                             XiaozhiActivationResult &result) {
  if (!fetch_activation(request, result)) {
    return false;
  }
  if (result.activation_challenge.empty()) {
    result.activation_pending = !result.activation_code.empty();
    return true;
  }

  static JsonDocument doc;
  doc.clear();
  doc["challenge"] = result.activation_challenge;
  std::string body;
  serializeJson(doc, body);
  std::string response_body;
  int status_code = 0;
  std::string transport_error;
  const std::string url = activation_activate_url(request);
  if (!http_post_json(request, url.c_str(), body, response_body, status_code,
                      transport_error)) {
    result.error = "activation_confirm_failed";
    result.transport_error = std::move(transport_error);
    return false;
  }
  if (status_code == 202) {
    result.activation_pending = true;
    return true;
  }
  if (status_code != 200) {
    result.error =
        "activation_confirm_bad_status_" + std::to_string(status_code);
    result.transport_error = std::move(transport_error);
    return false;
  }
  return fetch_activation(request, result);
}
