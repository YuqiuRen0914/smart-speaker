#include <cassert>
#include <cstring>
#include <map>
#include <array>
#include <iostream>
#include "esp_now.h"
#include "lunaredu_lunaros/network/espnow.h"
#include "lunaredu_lunaros/kernel/service_executor.h"
#include "network/espnow/espnow_internal.h"
#include "network/wifi/wifi_radio_policy.h"

using namespace lunaredu::lunaros;
using Key = std::array<uint8_t, 6>;
std::map<Key, esp_now_peer_info_t> peers;
esp_now_recv_cb_t receive_callback = nullptr;
bool running = false, fail_receive_registration = false, channel_busy = false;
int channel = 1, dispatches = 0, deliveries = 0, sends = 0;
int8_t tx_power = 80;
Key key(const uint8_t *mac) { Key k{}; std::memcpy(k.data(), mac, 6); return k; }
int esp_now_init() { running = true; return 0; }
int esp_now_deinit() { running = false; peers.clear(); return 0; }
int esp_now_register_send_cb(void (*)(const wifi_tx_info_t *, int)) { return 0; }
int esp_now_register_recv_cb(esp_now_recv_cb_t cb) {
  if (fail_receive_registration) return -1;
  receive_callback = cb; return 0;
}
int esp_now_unregister_send_cb() { return 0; }
int esp_now_unregister_recv_cb() { receive_callback = nullptr; return 0; }
bool esp_now_is_peer_exist(const uint8_t *mac) { return peers.count(key(mac)) != 0; }
int esp_now_add_peer(const esp_now_peer_info_t *peer) { peers[key(peer->peer_addr)] = *peer; return 0; }
int esp_now_mod_peer(const esp_now_peer_info_t *peer) { return esp_now_add_peer(peer); }
int esp_now_get_peer(const uint8_t *mac, esp_now_peer_info_t *out) {
  if (!esp_now_is_peer_exist(mac)) return -1;
  *out = peers[key(mac)]; return 0;
}
int esp_now_del_peer(const uint8_t *mac) { peers.erase(key(mac)); return 0; }
int esp_now_send(const uint8_t *, const uint8_t *, size_t) { ++sends; return running ? 0 : -1; }
int esp_wifi_get_mac(int, uint8_t *mac) { std::memset(mac, 2, 6); return 0; }
int esp_wifi_set_max_tx_power(int8_t value) { tx_power = value; return 0; }
int esp_wifi_get_max_tx_power(int8_t *value) { *value = tx_power; return 0; }
int wifi_channel() { return channel; }
bool wifi_set_radio_channel(uint8_t next, std::string &error) {
  if (channel_busy) { error = "wifi_radio_busy"; return false; }
  channel = next; error.clear(); return true;
}
namespace lunaredu::lunaros {
bool invoke_service_operation(const char *owner, ServiceOperation operation,
                              void *context, std::string &error, uint32_t) {
  assert(std::strcmp(owner, kNetworkServiceExecutor) == 0);
  ++dispatches; ServiceOperationResult result; operation(context, result);
  error = result.error; return result.ok;
}
}
void receive(const EspNowPacketView &packet, void *) {
  assert(packet.size == 1 && packet.data[0] == 42); ++deliveries;
}
int main() {
  using internal::RadioChannelDecision;
  using internal::decide_radio_channel;
  assert(decide_radio_channel(false,false,false,false,false,1,6) == RadioChannelDecision::Unavailable);
  assert(decide_radio_channel(true,true,false,false,false,1,1) == RadioChannelDecision::Busy);
  assert(decide_radio_channel(true,false,true,false,false,1,6) == RadioChannelDecision::Busy);
  assert(decide_radio_channel(true,false,false,true,false,1,6) == RadioChannelDecision::Conflict);
  assert(decide_radio_channel(true,false,false,false,true,1,6) == RadioChannelDecision::Conflict);
  assert(decide_radio_channel(true,false,false,true,true,6,6) == RadioChannelDecision::Keep);
  assert(decide_radio_channel(true,false,false,false,false,1,6) == RadioChannelDecision::Retune);
  assert(decide_radio_channel(true,false,false,false,false,1,15) == RadioChannelDecision::Unavailable);
  std::string error;
  assert(internal::start_espnow_on_owner({}, error) && !running);
  fail_receive_registration = true;
  assert(!internal::start_espnow_on_owner({true,1,0}, error) && !running && !espnow_ready());
  fail_receive_registration = false;
  assert(internal::start_espnow_on_owner({true,1,20}, error) && running);
  assert(espnow_status().local_mac[0] == 2);
  assert(espnow_status().max_tx_power_quarter_dbm == 20 && tx_power == 20);
  const uint8_t mac[6] = {2,3,4,5,6,7};
  const uint8_t data = 42;
  assert(espnow_register_peer(mac, 1, false, error));
  assert(espnow_send(mac, &data, 1, error) && sends == 1);
  assert(!espnow_send(mac, &data, 251, error) && sends == 1);
  // Re-registering a peer updates its channel; it must not silently keep old data.
  assert(espnow_register_peer(mac, 6, false, error));
  assert(!espnow_send(mac, &data, 1, error) && error == "espnow_peer_channel_mismatch");
  channel_busy = true;
  assert(!espnow_set_channel(6, error) && channel == 1);
  channel_busy = false;
  assert(espnow_set_channel(6, error));
  assert(espnow_send(mac, &data, 1, error));
  assert(espnow_send_broadcast(&data, 1, 3, error));
  assert(channel == 3 && espnow_status().channel == 3);
  channel = 11;
  internal::poll_espnow_on_owner();
  assert(espnow_status().channel == 11);
  assert(espnow_register_peer(mac, 0, false, error));
  assert(espnow_send(mac, &data, 1, error));
  espnow_set_receive_handler(receive, nullptr);
  wifi_pkt_rx_ctrl_t rx{-55}; esp_now_recv_info_t info{mac, &rx};
  uint8_t oversized[251] = {};
  receive_callback(&info, oversized, 251);
  for (int i = 0; i < 9; ++i) receive_callback(&info, &data, 1);
  assert(deliveries == 0 && espnow_status().dropped_packets == 2);
  tick_espnow(); assert(deliveries == 8);
  tick_espnow(); assert(deliveries == 8);
  receive_callback(&info, &data, 1);
  internal::stop_espnow_on_owner();
  tick_espnow(); assert(deliveries == 8 && !running && !espnow_ready());
  assert(!espnow_send(mac, &data, 1, error));
  assert(dispatches >= 10);
  assert(internal::start_espnow_on_owner({true,1,0}, error));
  assert(!espnow_send(mac, &data, 1, error)); // peers do not survive a radio restart
  internal::stop_espnow_on_owner();
  std::cout << "ESP-NOW owner, lifecycle, channel and receive tests passed\n";
}
