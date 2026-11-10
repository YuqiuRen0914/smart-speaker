#include "lunaredu_device_profiles.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <algorithm>

#include <ArduinoJson.h>
#include "platform/cold_storage.h"
#include "memory/json_workspace.h"
#include <esp_crt_bundle.h>
#include <mqtt_client.h>

#include <esp_log.h>
#include <esp_memory_utils.h>
#include "configuration/device_config.h"
#include "system/identity.h"
#include "platform_link_http.h"
#include "platform_link_operations.h"
#include "platform_link_state.h"
#include "lunaros_network_tls.h"
#include "platform_link_wire.h"

namespace lunaredu::lunaros::platform_link {
namespace {

constexpr const char *kTag = "platform_link_realtime";
LUNAROS_COLD_BSS JsonWorkspace g_json_workspace{16384};
// Used only on the SDK MQTT callback owner, which stop/destroy joins. The
// string's SSO self-pointer needs initialized data; external BSS discards it
// when GCC constant-initializes this control block. Payload allocation is
// independent of the 24-byte control block's internal placement.
std::string g_reply_body;
esp_mqtt_client_handle_t g_client = nullptr;
std::atomic<bool> g_subscribed{false};
std::atomic<uint32_t> g_received_frames{0};
std::atomic<uint32_t> g_published_replies{0};
LUNAROS_COLD_BSS std::array<std::array<char, 69>, protocol::contract::operation_recent_request_count> g_recent_request_ids{};
size_t g_recent_request_next = 0;
std::string g_endpoint;
std::shared_ptr<const NetworkTlsPolicy> g_tls_policy;
std::string g_serial;
std::string g_down_topic;
std::string g_subscription_topic;
std::string g_up_topic;
int g_binding_epoch = 0;
uint32_t g_last_config_check_ms = 0;
bool g_was_subscribed = false;

void process_frame(std::string_view body);

bool remember_request_id(const std::string &request_id) {
  for (const auto &recent : g_recent_request_ids) {
    if (request_id == recent.data()) return false;
  }
  auto &slot = g_recent_request_ids[g_recent_request_next++ % g_recent_request_ids.size()];
  slot.fill(0);
  std::memcpy(slot.data(), request_id.data(), request_id.size());
  return true;
}

void notify_channel_lost() {
  if (const auto callback = runtime_config().on_realtime_channel_lost)
    callback();
}

std::string mqtt_endpoint(const std::string &origin) {
  if (origin.rfind("https://", 0) == 0) return "wss://" + origin.substr(8) + "/mqtt";
  return {};
}

void mqtt_event(void *, esp_event_base_t, int32_t event_id, void *event_data) {
  auto *event = static_cast<esp_mqtt_event_handle_t>(event_data);
  if (event == nullptr) return;
  switch (event_id) {
  case MQTT_EVENT_CONNECTED:
    ESP_LOGI(kTag, "realtime control channel connected");
    (void)esp_mqtt_client_subscribe_single(event->client, g_subscription_topic.c_str(), 1);
    break;
  case MQTT_EVENT_SUBSCRIBED:
    g_subscribed.store(true);
    ESP_LOGI(kTag, "realtime control channel subscribed");
    break;
  case MQTT_EVENT_DISCONNECTED:
    g_subscribed.store(false);
    ESP_LOGW(kTag, "realtime control channel disconnected");
    break;
  case MQTT_EVENT_DATA:
    if (g_received_frames.fetch_add(1) < 64)
      ESP_LOGI(kTag, "realtime frame received topic_len=%d bytes=%d total=%d offset=%d",
           event->topic_len, event->data_len, event->total_data_len,
           event->current_data_offset);
    if (event->topic_len != static_cast<int>(g_down_topic.size()) ||
        std::memcmp(event->topic,g_down_topic.data(),g_down_topic.size()) != 0 ||
        event->data_len <= 0 || event->data_len > static_cast<int>(protocol::contract::operation_max_realtime_bytes) ||
        event->current_data_offset != 0 || event->data_len != event->total_data_len) return;
    // The product network tick can be delayed by display and cloud work.
    // Handle the non-durable frame on MQTT's own task so its acknowledgement
    // does not wait behind that scheduler.
    process_frame(std::string_view(event->data,size_t(event->data_len)));
    break;
  default:
    break;
  }
}

void close_channel() {
  g_subscribed.store(false);
  if (g_client != nullptr) {
    esp_mqtt_client_stop(g_client);
    esp_mqtt_client_destroy(g_client);
    g_client = nullptr;
  }
  g_json_workspace.retire();std::string().swap(g_reply_body);
  g_tls_policy.reset();
}

void start_channel(const std::string &endpoint, const std::string &serial,
                   const std::string &credential, int binding_epoch) {
  g_endpoint = endpoint;
  g_serial = serial;
  g_binding_epoch = binding_epoch;
  const std::string root = "lunaredu/hardware/v2/devices/" + serial + "/";
  g_down_topic = protocol::profiles::topic(protocol::profiles::mqtt_control_down,serial);
  g_subscription_topic = root + "commands/#";
  g_up_topic = protocol::profiles::topic(protocol::profiles::mqtt_control_up,serial);
  const std::string client_id = "control-" + serial;
  const std::string username = "device:" + serial;
  esp_mqtt_client_config_t config = {};
  config.broker.address.uri = endpoint.c_str();
  g_tls_policy = network_tls_policy_for_url(endpoint);
  if (endpoint.rfind("wss://", 0) == 0) {
    if (g_tls_policy->mode == "custom_ca") config.broker.verification.certificate = g_tls_policy->ca_pem.c_str();
    else config.broker.verification.crt_bundle_attach = g_tls_policy->mode == "skip_verify"
        ? network_tls_skip_verification_attach : esp_crt_bundle_attach;
  }
  config.credentials.client_id = client_id.c_str();
  config.credentials.username = username.c_str();
  config.credentials.authentication.password = credential.c_str();
  // A reconnect must not replay old motor commands from a persistent session.
  config.session.disable_clean_session = false;
  config.session.keepalive = 15;
  config.network.timeout_ms = 2500;
  // WSS certificate verification exceeds a 4 KiB stack on ESP-IDF 6.1.
  // Keep headroom for the TLS handshake and realtime command callbacks.
  config.task.stack_size = 8192;
  config.buffer.size = 4096;
  config.buffer.out_size = 4096;
  g_client = esp_mqtt_client_init(&config);
  if (g_client == nullptr) { ESP_LOGW(kTag, "realtime MQTT allocation failed"); return; }
  if (esp_mqtt_client_register_event(g_client, MQTT_EVENT_ANY, mqtt_event, nullptr) != ESP_OK ||
      esp_mqtt_client_start(g_client) != ESP_OK) {
    esp_mqtt_client_destroy(g_client);
    g_client = nullptr;
    ESP_LOGW(kTag, "realtime MQTT start failed");
  }
}

void publish_reply(JsonDocument &reply) {
  if (g_client == nullptr || !g_subscribed.load()) return;
  auto &body=g_reply_body;body.clear();
  serializeJson(reply, body);
  if (body.size() > protocol::contract::operation_max_realtime_result_bytes) {
    reply["payload"]["result"].to<JsonObject>();
    reply["payload"]["ok"] = false;
    reply["payload"]["error"]["code"] = "realtime_reply_too_large";
    body.clear();
    serializeJson(reply, body);
  }
  const int published = esp_mqtt_client_publish(g_client, g_up_topic.c_str(),
                                                body.c_str(), static_cast<int>(body.size()), 1, 0);
  if (g_published_replies.fetch_add(1) < 64 || published < 0)
    ESP_LOGI(kTag, "realtime reply queued bytes=%u result=%d", static_cast<unsigned>(body.size()), published);
}

void process_frame(std::string_view body) {
  struct ReplyReturn {~ReplyReturn(){
    std::fill(g_reply_body.begin(),g_reply_body.end(),'\0');g_reply_body.clear();
    if(g_reply_body.capacity()>8192||!esp_ptr_external_ram(g_reply_body.data()))std::string().swap(g_reply_body);
  }} reply_return;
  JsonDocument frame{&g_json_workspace};
  if (deserializeJson(frame,body.data(),body.size()) || !wire::operation(frame.as<JsonObjectConst>(),"realtime") ||
      frame["session"]["id"]!=protocol::device_session_id(g_serial)) return;
  const auto authority=frame["payload"]["authority"].as<JsonObjectConst>();
  if (!authority["access_epoch"].is<int>() || !authority["shared"].is<bool>() ||
      !authority["shared_control_enabled"].is<bool>()) return;
  const std::string request_id=wire::text(frame["request_id"]);
  // QoS 1 duplicates cannot run the same actuator operation twice.
  if (!remember_request_id(request_id)) return;
  const auto binding=pairing_snapshot();
  JsonDocument data{&g_json_workspace},reply{&g_json_workspace};bool ok=false;std::string error;
  if (!wire::realtime_authorized(frame.as<JsonObjectConst>(),g_serial,{g_binding_epoch,binding.binding_epoch,
      binding.access_epoch,binding.state==PlatformLinkBindingState::Bound,binding.revocation_requested,
      binding.persistence_pending,binding.shared_control_enabled})) {
    error="stale_teleop_frame";
  } else {
    if (!authority["shared"].as<bool>()) update_shared_access(authority["access_epoch"].as<int>(),
      authority["shared_control_enabled"].as<bool>(),authority["control_manager_id"]|"");
    ok=execute_realtime_teleop(frame.as<JsonObjectConst>(),data);
    if (!ok) error=data["error"]|"teleop_failed";
  }
  wire::result(reply,frame.as<JsonObjectConst>(),ok,data["data"].as<JsonObjectConst>(),error);
  publish_reply(reply);
}

} // namespace

void stop_realtime_control_channel() {
  if (g_was_subscribed) notify_channel_lost();
  g_was_subscribed = false;
  close_channel();
  g_endpoint.clear();
  g_serial.clear();
  g_binding_epoch = 0;
}

void tick_realtime_control_channel() {
  if (!g_subscribed.load()) {
    if (g_was_subscribed) notify_channel_lost();
    g_was_subscribed = false;
  } else {
    g_was_subscribed = true;
  }
  const uint32_t now = monotonic_ms();
  if (now - g_last_config_check_ms < 3000) return;
  g_last_config_check_ms = now;
  const auto binding = pairing_snapshot();
  const std::string endpoint = mqtt_endpoint(platform_base_url());
  const std::string serial = hardware_serial_number();
  const std::string credential = device_cloud_credential();
  const bool should_connect = network_connected() &&
      runtime_config().remote_commands_enabled &&
      runtime_config().realtime_teleop_enabled &&
      binding.state == PlatformLinkBindingState::Bound &&
      !binding.revocation_requested && !binding.persistence_pending &&
      binding.binding_epoch > 0 && !binding.account_id.empty() &&
      !endpoint.empty() && !credential.empty();
  if (!should_connect || endpoint != g_endpoint || serial != g_serial ||
      binding.binding_epoch != g_binding_epoch) {
    stop_realtime_control_channel();
    if (should_connect) start_channel(endpoint, serial, credential, binding.binding_epoch);
  } else if (g_client == nullptr && should_connect) {
    start_channel(endpoint, serial, credential, binding.binding_epoch);
  }
}

} // namespace lunaredu::lunaros::platform_link
