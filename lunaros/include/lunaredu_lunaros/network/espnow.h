#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace lunaredu::lunaros {

inline constexpr size_t kEspNowMaximumPayloadBytes = 250;

// Installed through NetworkPolicy before Network starts. The Network service
// owns the STA radio and transport lifetime, including on radio-only products.
struct EspNowPolicy {
  bool enabled = false;
  uint8_t channel = 1;
  // ESP-IDF uses quarter-dBm units. Zero preserves the board/SDK default.
  int8_t max_tx_power_quarter_dbm = 0;
};

struct EspNowPacketView {
  const uint8_t *source_mac = nullptr;
  const uint8_t *data = nullptr;
  size_t size = 0;
  int rssi = 0;
};
using EspNowReceiveHandler = void (*)(const EspNowPacketView &, void *);

struct EspNowStatus {
  bool enabled = false;
  bool initialized = false;
  uint8_t channel = 0; // Observed radio channel, not the requested preference.
  int8_t max_tx_power_quarter_dbm = 0;
  std::array<uint8_t, 6> local_mac{};
  uint32_t sent_packets = 0; // Accepted by the driver; not delivery proof.
  uint32_t received_packets = 0;
  uint32_t dropped_packets = 0;
  int last_rssi = 0;
  int last_error = 0;
};

bool espnow_ready();
EspNowStatus espnow_status();
// Peer/send/channel operations execute on the fixed Network owner. Channel 0
// on a peer follows the current radio. Fixed peers must match that radio.
bool espnow_register_peer(const uint8_t mac[6], uint8_t channel,
                          bool encrypt, std::string &error);
bool espnow_unregister_peer(const uint8_t mac[6], std::string &error);
// Refuses retuning during scan/connection, association or SoftAP use.
bool espnow_set_channel(uint8_t channel, std::string &error);
bool espnow_send(const uint8_t mac[6], const void *data, size_t size,
                 std::string &error);
// Retunes an idle radio to channel before registering the broadcast peer.
bool espnow_send_broadcast(const void *data, size_t size, uint8_t channel,
                           std::string &error);
// Set and drain from one product owner. Callbacks run only while that owner
// drains this bounded queue, never on the Wi-Fi callback or Network task.
void espnow_set_receive_handler(EspNowReceiveHandler handler, void *context);
void tick_espnow();

} // namespace lunaredu::lunaros
