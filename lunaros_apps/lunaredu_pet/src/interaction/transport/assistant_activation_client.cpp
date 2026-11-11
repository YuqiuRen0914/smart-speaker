#include "interaction/transport/assistant_activation_client.h"

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
  doc["protocol"] = "lunaredu.wire";
  doc["family"] = "device";
  doc["profile"] = "mqtt";
  doc["version"] = 1;
  doc["hardware_serial"] = request.device_id;
  doc["language"] = "zh-CN";
  doc["uuid"] = request.client_id;
  doc["flash_size"] = lunaredu::system::SystemInfo::GetFlashSize();
  doc["minimum_free_heap_size"] =
      lunaredu::system::SystemInfo::GetMinimumFreeHeapSize();
  doc["mac_address"] = request.mac_address;
  doc["chip_model_name"] = lunaredu::system::SystemInfo::GetChipModelName();
  JsonObject board = doc["board"].to<JsonObject>();
  board["type"] = request.board_name;
  board["name"] = request.board_name;
  board["mac"] = request.mac_address;
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

  if (doc["protocol"] != "lunaredu.wire" || doc["family"] != "device" ||
      !doc["version"].is<int>() || doc["version"].as<int>() != 1 || doc["profile"] != "mqtt" ||
      !doc["binding"]["epoch"].is<int>()) {
    result.error = "lunarlink_bootstrap_protocol_invalid";
    return false;
  }
  JsonObject mqtt = doc["mqtt"].as<JsonObject>();
  result.binding_epoch = doc["binding"]["epoch"].as<int>();
  result.ok = true;
  result.mqtt_endpoint = mqtt["endpoint"] | "";
  result.has_mqtt = !result.mqtt_endpoint.empty();
  result.mqtt_client_id = mqtt["client_id"] | "";
  result.mqtt_username = mqtt["username"] | "";
  result.mqtt_password = mqtt["password"] | "";
  result.mqtt_publish_topic = mqtt["publish_topic"] | "";
  result.mqtt_subscribe_topic = mqtt["subscribe_topic"] | "";
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
  fetch_request.headers.push_back({"X-Lunaredu-Protocol", "lunaredu.wire"});
  fetch_request.headers.push_back({"X-Lunaredu-Family", "device"});
  fetch_request.headers.push_back({"X-Lunaredu-Version", "1"});
  fetch_request.headers.push_back({"X-Lunaredu-Profile", "mqtt"});
  fetch_request.headers.push_back({"Device-Token", request.device_token});
  fetch_request.headers.push_back({"Authorization", "Bearer " + request.device_token});
  fetch_request.headers.push_back({"Device-Id", request.device_id});
  fetch_request.headers.push_back({"Client-Id", request.client_id});
  fetch_request.headers.push_back({"User-Agent",
                                   "bread-compact-wifi-s3cam/pc-style-runtime"});
  fetch_request.headers.push_back({"Accept-Language", "zh-CN"});
  fetch_request.headers.push_back({"Content-Type", "application/json"});

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

} // namespace

bool xiaozhi_activate_device(const XiaozhiActivationRequest &request,
                             XiaozhiActivationResult &result) {
  result = {};
  if (request.device_id.empty() || request.device_token.empty() || request.binding_epoch <= 0) {
    result.error = "lunarlink_identity_not_ready";
    return false;
  }
  std::string body, transport_error;
  int status = 0;
  if (!http_post_json(request, request.ota_url.c_str(), build_activation_payload(request),
                      body, status, transport_error) || status != 200) {
    result.error = "lunarlink_bootstrap_http_" + std::to_string(status);
    result.transport_error = std::move(transport_error);
    return false;
  }
  if (!parse_activation_response(body, result)) return false;
  if (result.binding_epoch != request.binding_epoch || !result.has_mqtt ||
      result.mqtt_client_id.empty() || result.mqtt_publish_topic.empty() ||
      result.mqtt_subscribe_topic.empty() || result.mqtt_subscribe_topic == "null") {
    result.ok = false;
    result.error = "lunarlink_bootstrap_identity_or_mqtt_invalid";
    return false;
  }
  return true;
}
