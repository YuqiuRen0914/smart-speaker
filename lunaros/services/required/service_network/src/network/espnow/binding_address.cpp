#include "lunaros_network_espnow.h"
#include "lunaros_network_espnow_binding.h"
#include <cstdio>
#include <cstring>

namespace lunaredu::lunaros::espnow_binding {
bool mac_equal(const uint8_t left[6], const uint8_t right[6]) {
  return left != nullptr && right != nullptr && std::memcmp(left, right, 6) == 0;
}

bool mac_is_unicast(const uint8_t mac[6]) {
  if (mac == nullptr || (mac[0] & 1U) != 0) return false;
  for (size_t index = 0; index < 6; ++index) {
    if (mac[index] != 0) return true;
  }
  return false;
}

bool binding_valid(const Binding &binding) {
  return binding.version == kBindingVersion && binding.paired != 0 &&
         binding.channel >= 1 && binding.channel <= 14 &&
         mac_is_unicast(binding.peer_mac) && binding.nonce != 0;
}

void mac_to_string(const uint8_t mac[6], std::string &result) {
  if (mac == nullptr) { result.clear(); return; }
  char buffer[18] = {};
  std::snprintf(buffer, sizeof(buffer), "%02X:%02X:%02X:%02X:%02X:%02X",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  result.assign(buffer, sizeof(buffer)-1);
}

std::string mac_to_string(const uint8_t mac[6]) {
  std::string result;mac_to_string(mac,result);return result;
}

bool mac_from_string(const std::string &value, uint8_t mac[6]) {
  if (mac == nullptr) return false;
  unsigned int bytes[6] = {};
  if (std::sscanf(value.c_str(), "%x:%x:%x:%x:%x:%x", &bytes[0], &bytes[1],
                  &bytes[2], &bytes[3], &bytes[4], &bytes[5]) != 6) {
    return false;
  }
  for (size_t index = 0; index < 6; ++index) {
    if (bytes[index] > 0xff) return false;
    mac[index] = static_cast<uint8_t>(bytes[index]);
  }
  return mac_is_unicast(mac);
}

}
