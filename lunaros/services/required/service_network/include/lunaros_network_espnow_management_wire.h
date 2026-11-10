#pragma once
#include <cstddef>
#include <cstdint>
namespace lunaredu::lunaros {
// LEM1 is inside the official encrypted ESP-NOW payload. Binding the family
// inside the authenticated bytes prevents relabelling a valid CCM frame.
inline constexpr size_t kEspNowManagementHeaderBytes=20;
struct EspNowManagementEnvelope {
  uint8_t family=0,flags=0; // Request=0, reply=1, key-confirmed welcome=2.
  uint64_t nonce=0;
  uint32_t sequence=0;
  const uint8_t *payload=nullptr;
  size_t size=0;
};
bool espnow_encode_management(const EspNowManagementEnvelope &,uint8_t *,size_t &);
bool espnow_decode_management(const uint8_t *,size_t,EspNowManagementEnvelope &);
}
