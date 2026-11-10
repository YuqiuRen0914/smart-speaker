#include "interaction/conversation/assistant_client_id.h"

#include <cstdio>
#include <mutex>

#include <esp_random.h>

#include "lunaredu_lunaros/persistence/settings.h"

namespace lunaredu::pet::conversation {
namespace {

constexpr const char *kSettingsNamespace = "xiaozhi";
constexpr const char *kClientIdKey = "client_id";
constexpr const char *kLegacySettingsNamespace = "board";
constexpr const char *kLegacyClientIdKey = "uuid";

std::mutex g_client_id_mutex;
std::string g_client_id;

std::string generate_client_id() {
  uint8_t uuid[16] = {};
  esp_fill_random(uuid, sizeof(uuid));
  uuid[6] = static_cast<uint8_t>((uuid[6] & 0x0F) | 0x40);
  uuid[8] = static_cast<uint8_t>((uuid[8] & 0x3F) | 0x80);

  char buffer[37] = {};
  std::snprintf(buffer, sizeof(buffer),
                "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-"
                "%02x%02x%02x%02x%02x%02x",
                uuid[0], uuid[1], uuid[2], uuid[3], uuid[4], uuid[5],
                uuid[6], uuid[7], uuid[8], uuid[9], uuid[10], uuid[11],
                uuid[12], uuid[13], uuid[14], uuid[15]);
  return std::string(buffer);
}

} // namespace

const std::string &assistant_client_id() {
  std::lock_guard<std::mutex> lock(g_client_id_mutex);
  if (!g_client_id.empty()) {
    return g_client_id;
  }

  g_client_id = lunaredu::lunaros::settings_get_string(
      kSettingsNamespace, kClientIdKey);
  if (!g_client_id.empty()) {
    return g_client_id;
  }

  // Preserve deployed activations while moving protocol-owned state out of
  // the generic LunarOS identity module.
  g_client_id = lunaredu::lunaros::settings_get_string(
      kLegacySettingsNamespace, kLegacyClientIdKey);
  if (g_client_id.empty()) {
    g_client_id = generate_client_id();
  }
  if (lunaredu::lunaros::settings_set_string(
          kSettingsNamespace, kClientIdKey, g_client_id)) {
    (void)lunaredu::lunaros::settings_erase(kLegacySettingsNamespace,
                                            kLegacyClientIdKey);
  }
  return g_client_id;
}

} // namespace lunaredu::pet::conversation
