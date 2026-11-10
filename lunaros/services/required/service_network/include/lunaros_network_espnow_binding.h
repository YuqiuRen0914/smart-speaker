#pragma once

#include <stdint.h>
#include <cstddef>

#include <string>

namespace lunaredu::lunaros::espnow_binding {

constexpr uint32_t kBindingVersion = 1;
constexpr uint8_t kDefaultChannel = 1;

// Persisted ESP-NOW application binding. This record does not register a
// transport peer or initiate pairing; the application owns those transitions.
// Keep the deployed v1 layout and caller-selected namespace/key unchanged.
struct __attribute__((packed)) Binding {
  uint32_t version = kBindingVersion;
  uint8_t paired = 0;
  uint8_t channel = kDefaultChannel;
  uint8_t peer_mac[6] = {};
  uint64_t nonce = 0;
};

static_assert(sizeof(Binding) == 20);
static_assert(offsetof(Binding, nonce) == 12);

enum class BindingReadResult { Found, Missing, Failed };
BindingReadResult load_binding(const char *name_space, Binding &binding, std::string &error,
                  const char *key = "binding");
bool save_binding(const char *name_space, const Binding &binding,
                  std::string &error, const char *key = "binding");
bool clear_binding(const char *name_space, std::string &error,
                   const char *key = "binding");

bool binding_valid(const Binding &binding);
bool mac_equal(const uint8_t left[6], const uint8_t right[6]);
bool mac_is_unicast(const uint8_t mac[6]);
std::string mac_to_string(const uint8_t mac[6]);
void mac_to_string(const uint8_t mac[6], std::string &result);
bool mac_from_string(const std::string &value, uint8_t mac[6]);
uint64_t make_binding_nonce();

}  // namespace lunaredu::lunaros::espnow_binding
