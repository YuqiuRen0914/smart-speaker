#include "lunaredu_lunaros/network/espnow.h"
#include "network/espnow/espnow_internal.h"
#include "lunaredu_lunaros/kernel/service_executor.h"
#include "network/wifi/wifi_manager.h"

#include "sdkconfig.h"

#if CONFIG_LUNAREDU_NETWORK_ENABLE_ESPNOW

#include <cstring>

#include <esp_err.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>

namespace lunaredu::lunaros {
namespace {

constexpr size_t kReceiveQueueSize = 8;
constexpr uint8_t kBroadcastMac[6] = {0xff, 0xff, 0xff,
                                      0xff, 0xff, 0xff};

struct ReceiveFrame {
  bool occupied = false;
  uint8_t source_mac[6] = {};
  uint8_t data[kEspNowMaximumPayloadBytes] = {};
  size_t size = 0;
  int rssi = 0;
};

portMUX_TYPE g_lock = portMUX_INITIALIZER_UNLOCKED;
EspNowStatus g_status;
EspNowReceiveHandler g_receive_handler = nullptr;
void *g_receive_context = nullptr;
ReceiveFrame g_receive_queue[kReceiveQueueSize];
size_t g_receive_write_index = 0;
size_t g_receive_read_index = 0;
wifi_interface_t g_interface = WIFI_IF_STA;

void set_error_locked(esp_err_t error) {
  g_status.last_error = static_cast<int>(error);
}

void clear_receive_queue_locked() {
  for (ReceiveFrame &frame : g_receive_queue) {
    frame = {};
  }
  g_receive_write_index = 0;
  g_receive_read_index = 0;
}

void on_send(const wifi_tx_info_t *, esp_now_send_status_t) {}

void on_receive(const esp_now_recv_info_t *info, const uint8_t *data,
                int data_length) {
  if (info == nullptr || info->src_addr == nullptr || data == nullptr ||
      data_length <= 0) {
    return;
  }

  portENTER_CRITICAL(&g_lock);
  if (!g_status.initialized ||
      static_cast<size_t>(data_length) > kEspNowMaximumPayloadBytes ||
      g_receive_queue[g_receive_write_index].occupied) {
    g_status.dropped_packets += 1;
    portEXIT_CRITICAL(&g_lock);
    return;
  }

  ReceiveFrame &frame = g_receive_queue[g_receive_write_index];
  frame.occupied = true;
  std::memcpy(frame.source_mac, info->src_addr, sizeof(frame.source_mac));
  frame.size = static_cast<size_t>(data_length);
  std::memcpy(frame.data, data, frame.size);
  frame.rssi = info->rx_ctrl == nullptr ? 0 : info->rx_ctrl->rssi;
  g_receive_write_index = (g_receive_write_index + 1) % kReceiveQueueSize;
  g_status.received_packets += 1;
  g_status.last_rssi = frame.rssi;
  portEXIT_CRITICAL(&g_lock);
}

bool set_operation_error(esp_err_t result, std::string &error) {
  if (result == ESP_OK) {
    error.clear();
    return true;
  }
  portENTER_CRITICAL(&g_lock);
  set_error_locked(result);
  portEXIT_CRITICAL(&g_lock);
  error = esp_err_to_name(result);
  return false;
}

}  // namespace

bool internal::start_espnow_on_owner(const EspNowPolicy &policy, std::string &error) {
  error.clear();
  portENTER_CRITICAL(&g_lock);
  g_status.enabled = policy.enabled;
  portEXIT_CRITICAL(&g_lock);
  if (!policy.enabled) return true;
  if (espnow_ready()) return true;
  // Network has already started the STA radio and selected its channel.
  if (policy.max_tx_power_quarter_dbm != 0) {
    const esp_err_t power_result =
        esp_wifi_set_max_tx_power(policy.max_tx_power_quarter_dbm);
    if (power_result != ESP_OK) {
      return set_operation_error(power_result, error);
    }
  }
  esp_err_t result = esp_now_init();
  if (result != ESP_OK) {
    return set_operation_error(result, error);
  }
  result = esp_now_register_send_cb(on_send);
  if (result != ESP_OK) {
    esp_now_deinit();
    return set_operation_error(result, error);
  }
  result = esp_now_register_recv_cb(on_receive);
  if (result != ESP_OK) {
    esp_now_unregister_send_cb();
    esp_now_deinit();
    return set_operation_error(result, error);
  }

  uint8_t local_mac[6] = {};
  (void)esp_wifi_get_mac(WIFI_IF_STA, local_mac);
  int8_t actual_tx_power = 0;
  (void)esp_wifi_get_max_tx_power(&actual_tx_power);
  const uint8_t channel = static_cast<uint8_t>(wifi_channel());
  portENTER_CRITICAL(&g_lock);
  std::memcpy(g_status.local_mac.data(), local_mac, sizeof(local_mac));
  g_status.initialized = true;
  g_status.channel = channel;
  g_status.max_tx_power_quarter_dbm = actual_tx_power;
  g_status.last_error = ESP_OK;
  g_interface = WIFI_IF_STA;
  clear_receive_queue_locked();
  portEXIT_CRITICAL(&g_lock);
  error.clear();
  return true;
}

void internal::stop_espnow_on_owner() {
  portENTER_CRITICAL(&g_lock);
  const bool initialized = g_status.initialized;
  g_status.initialized = false;
  g_receive_handler = nullptr;
  g_receive_context = nullptr;
  g_interface = WIFI_IF_STA;
  clear_receive_queue_locked();
  portEXIT_CRITICAL(&g_lock);

  if (initialized) {
    esp_now_unregister_recv_cb();
    esp_now_unregister_send_cb();
    esp_now_deinit();
  }

}

void tick_espnow() {
  for (size_t count = 0; count < kReceiveQueueSize; ++count) {
    ReceiveFrame frame;
    EspNowReceiveHandler handler = nullptr;
    void *context = nullptr;

    portENTER_CRITICAL(&g_lock);
    if (!g_receive_queue[g_receive_read_index].occupied) {
      portEXIT_CRITICAL(&g_lock);
      return;
    }
    frame = g_receive_queue[g_receive_read_index];
    g_receive_queue[g_receive_read_index] = {};
    g_receive_read_index = (g_receive_read_index + 1) % kReceiveQueueSize;
    handler = g_receive_handler;
    context = g_receive_context;
    portEXIT_CRITICAL(&g_lock);

    if (handler != nullptr) {
      const EspNowPacketView packet = {
          .source_mac = frame.source_mac,
          .data = frame.data,
          .size = frame.size,
          .rssi = frame.rssi,
      };
      handler(packet, context);
    }
  }
}

bool espnow_ready() {
  portENTER_CRITICAL(&g_lock);
  const bool ready = g_status.initialized;
  portEXIT_CRITICAL(&g_lock);
  return ready;
}

EspNowStatus espnow_status() {
  portENTER_CRITICAL(&g_lock);
  const EspNowStatus status = g_status;
  portEXIT_CRITICAL(&g_lock);
  return status;
}

bool register_peer_on_owner(const uint8_t mac[6], uint8_t channel, bool encrypt,
                          std::string &error) {
  if (!espnow_ready() || mac == nullptr || channel > 14) {
    error = "invalid_state_or_peer";
    return false;
  }
  esp_now_peer_info_t peer = {};
  std::memcpy(peer.peer_addr, mac, sizeof(peer.peer_addr));
  peer.channel = channel;
  portENTER_CRITICAL(&g_lock);
  peer.ifidx = g_interface;
  portEXIT_CRITICAL(&g_lock);
  if (encrypt) {
    error = "espnow_encryption_key_required";
    return false;
  }
  peer.encrypt = false;
  return set_operation_error(esp_now_is_peer_exist(mac)
      ? esp_now_mod_peer(&peer) : esp_now_add_peer(&peer), error);
}

bool unregister_peer_on_owner(const uint8_t mac[6], std::string &error) {
  if (!espnow_ready() || mac == nullptr) {
    error = "invalid_state_or_peer";
    return false;
  }
  if (!esp_now_is_peer_exist(mac)) {
    error.clear();
    return true;
  }
  return set_operation_error(esp_now_del_peer(mac), error);
}

bool set_channel_on_owner(uint8_t channel, std::string &error) {
  if (!espnow_ready() || channel == 0 || channel > 14) {
    error = "invalid_state_or_channel";
    return false;
  }
  if (!wifi_set_radio_channel(channel, error)) {
    return false;
  }
  portENTER_CRITICAL(&g_lock);
  g_status.channel = channel;
  portEXIT_CRITICAL(&g_lock);
  return true;
}

bool send_on_owner(const uint8_t mac[6], const void *data, size_t size,
                 std::string &error) {
  if (!espnow_ready() || mac == nullptr || data == nullptr || size == 0 ||
      size > kEspNowMaximumPayloadBytes) {
    error = "invalid_state_or_payload";
    return false;
  }
  esp_now_peer_info_t peer = {};
  if (!set_operation_error(esp_now_get_peer(mac, &peer), error)) return false;
  if (peer.channel != 0 && peer.channel != wifi_channel()) {
    error = "espnow_peer_channel_mismatch";
    return false;
  }
  const esp_err_t result =
      esp_now_send(mac, static_cast<const uint8_t *>(data), size);
  if (!set_operation_error(result, error)) {
    return false;
  }
  portENTER_CRITICAL(&g_lock);
  g_status.sent_packets += 1;
  portEXIT_CRITICAL(&g_lock);
  return true;
}

bool broadcast_on_owner(const void *data, size_t size, uint8_t channel,
                           std::string &error) {
  if (!set_channel_on_owner(channel, error)) {
    return false;
  }
  if (!register_peer_on_owner(kBroadcastMac, channel, false, error)) {
    return false;
  }
  return send_on_owner(kBroadcastMac, data, size, error);
}

void espnow_set_receive_handler(EspNowReceiveHandler handler, void *context) {
  portENTER_CRITICAL(&g_lock);
  g_receive_handler = handler;
  g_receive_context = context;
  portEXIT_CRITICAL(&g_lock);
}

}  // namespace lunaredu::lunaros

#else
namespace lunaredu::lunaros {
bool internal::start_espnow_on_owner(const EspNowPolicy &policy, std::string &error) {
  error = policy.enabled ? "espnow_disabled_in_build" : "";
  return !policy.enabled;
}
void internal::stop_espnow_on_owner() {}
void tick_espnow() {}
bool espnow_ready() { return false; }
EspNowStatus espnow_status() { return {}; }
void espnow_set_receive_handler(EspNowReceiveHandler, void *) {}
} // namespace lunaredu::lunaros
#endif

namespace lunaredu::lunaros {
void internal::poll_espnow_on_owner() {
#if CONFIG_LUNAREDU_NETWORK_ENABLE_ESPNOW
  if (!espnow_ready()) return;
  const uint8_t channel = static_cast<uint8_t>(wifi_channel());
  portENTER_CRITICAL(&g_lock);
  g_status.channel = channel;
  portEXIT_CRITICAL(&g_lock);
#endif
}
namespace {
enum class Operation { AddPeer, RemovePeer, Channel, Send, Broadcast };
struct Request {
  Operation operation;
  const uint8_t *mac = nullptr;
  uint8_t channel = 0;
  bool encrypt = false;
  const void *data = nullptr;
  size_t size = 0;
};
void execute(Request &request, ServiceOperationResult &result) {
#if CONFIG_LUNAREDU_NETWORK_ENABLE_ESPNOW
  switch (request.operation) {
  case Operation::AddPeer:
    result.ok = register_peer_on_owner(request.mac, request.channel,
                                       request.encrypt, result.error); break;
  case Operation::RemovePeer:
    result.ok = unregister_peer_on_owner(request.mac, result.error); break;
  case Operation::Channel:
    result.ok = set_channel_on_owner(request.channel, result.error); break;
  case Operation::Send:
    result.ok = send_on_owner(request.mac, request.data, request.size,
                              result.error); break;
  case Operation::Broadcast:
    result.ok = broadcast_on_owner(request.data, request.size,
                                   request.channel, result.error); break;
  }
#else
  (void)request;
  result.ok = false;
  result.error = "espnow_disabled_in_build";
#endif
}
bool invoke(Request &request, std::string &error) {
  return invoke_service_operation(kNetworkServiceExecutor, execute, request, error);
}
} // namespace
bool espnow_register_peer(const uint8_t mac[6], uint8_t channel, bool encrypt,
                          std::string &error) {
  Request request{Operation::AddPeer, mac, channel, encrypt};
  return invoke(request, error);
}
bool espnow_unregister_peer(const uint8_t mac[6], std::string &error) {
  Request request{Operation::RemovePeer, mac};
  return invoke(request, error);
}
bool espnow_set_channel(uint8_t channel, std::string &error) {
  Request request{Operation::Channel, nullptr, channel};
  return invoke(request, error);
}
bool espnow_send(const uint8_t mac[6], const void *data, size_t size,
                 std::string &error) {
  Request request{Operation::Send, mac, 0, false, data, size};
  return invoke(request, error);
}
bool espnow_send_broadcast(const void *data, size_t size, uint8_t channel,
                           std::string &error) {
  Request request{Operation::Broadcast, nullptr, channel, false, data, size};
  return invoke(request, error);
}
} // namespace lunaredu::lunaros
