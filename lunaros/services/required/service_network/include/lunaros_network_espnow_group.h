#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
namespace lunaredu::lunaros {
class KernelRuntime;
// Keep the 22-byte session envelope inside the legacy secure payload budget.
inline constexpr size_t kEspNowGroupPayloadBytes=196;
enum class EspNowGroupRole:uint8_t { Transmitter, Receiver };
struct EspNowGroupPacket {
  std::array<uint8_t,6> group{},source{};
  uint32_t sequence=0;
  const uint8_t *data=nullptr;size_t size=0;
};
struct EspNowGroupWindow {
  std::array<uint8_t,6> group{},transmitter{};
  EspNowGroupRole role=EspNowGroupRole::Receiver;
  const uint8_t *key=nullptr; // Exactly 32 bytes, consumed before open returns.
  // Nonzero and strictly increasing during this OS boot, shared by members.
  // Products must provision a fresh key after a reboot; no persistent replay
  // counter or key distribution is implied by this volatile native API.
  uint64_t epoch=0;
  uint32_t duration_ms=10000; // 100..60000; no discovery or channel hopping.
  bool (*authorized)(void *)=nullptr; // Nonblocking, safe on Network/product owners.
  void *context=nullptr; // Remains alive until close succeeds.
  // Product-owner delivery from tick_espnow(), never the Wi-Fi/Network callback.
  // Packet bytes are borrowed only for the duration of this callback.
  void (*receive)(const EspNowGroupPacket &,void *)=nullptr;
  // Network-owner callback after physical retirement; nonblocking, no control
  // reentry. It may release context, including after partial open failure.
  void (*retired)(void *)=nullptr;
};
struct EspNowGroupStatus {
  bool supported=false,active=false,retiring=false;
  EspNowGroupRole role=EspNowGroupRole::Receiver;
  std::array<uint8_t,6> group{};
  uint8_t channel=0; // Window closes when the observed radio channel changes.
  uint32_t generation=0,remaining_ms=0,sent=0,received=0,dropped=0;
  std::string last_error;
};
// One ephemeral secure group. Shares Network's radio/CCM owner exclusively
// with management. Native caller supplies authority and fresh key/epoch.
// Appropriate for effects/timing events; no ACK or proof every member acted.
// Do not replace per-device motion authority or use this as an OTA installer.
bool espnow_open_group(KernelRuntime &,const EspNowGroupWindow &,std::string &);
bool espnow_send_group(KernelRuntime &,const void *,size_t,std::string &);
bool espnow_close_group(KernelRuntime &,std::string &);
EspNowGroupStatus espnow_group_status();
}
