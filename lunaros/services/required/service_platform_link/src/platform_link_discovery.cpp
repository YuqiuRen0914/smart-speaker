#include "platform_link_operations.h"

#include <atomic>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <utility>

#include <ArduinoJson.h>

#include <esp_log.h>
#include "configuration/device_config.h"
#include "system/identity.h"
#include "lunaros_network.h"
#include "platform_link_http.h"
#include "platform/system.h"
#include "platform_link_state.h"

namespace lunaredu::lunaros::platform_link {
namespace {

constexpr const char *kTag = "platform_link";

std::atomic<int64_t> g_discovery_expires_at_ms{0};
std::atomic<uint32_t> g_discovery_nonce{0};

// The server persists this announcement until its explicit expiry. Presence
// maintains online freshness; re-posting identical metadata does not renew the
// lease. Keep only the mutable key, never a second JSON body or credentials.
struct AnnouncementKey {
  uint32_t nonce = 0;
  int64_t expires_at_ms = 0;
  std::string address, name, origin;
  bool operator==(const AnnouncementKey &other) const {
    return nonce == other.nonce && expires_at_ms == other.expires_at_ms &&
           address == other.address && name == other.name && origin == other.origin;
  }
};
std::mutex g_announcement_mutex;
AnnouncementKey g_acknowledged;
bool g_announcement_acknowledged = false;
uint64_t g_announcement_generation = 0;

AnnouncementKey announcement_key() {
  return {g_discovery_nonce.load(), g_discovery_expires_at_ms.load(),
          network_snapshot().local_ip, device_identity_config().name,
          platform_base_url()};
}

} // namespace

void invalidate_discovery_announcement() {
  std::lock_guard<std::mutex> lock(g_announcement_mutex);
  g_announcement_acknowledged = false;
  ++g_announcement_generation;
}

void end_discovery() {
  g_discovery_expires_at_ms.store(0);
  g_discovery_nonce.store(0);
  std::lock_guard<std::mutex> lock(g_announcement_mutex);
  g_acknowledged = {};
  g_announcement_acknowledged = false;
  ++g_announcement_generation;
}

bool discovery_announcement_needed() {
  if (!discovery_active()) return false;
  const auto desired = announcement_key();
  if (desired.address.empty()) return false;
  std::lock_guard<std::mutex> lock(g_announcement_mutex);
  return !g_announcement_acknowledged || !(g_acknowledged == desired);
}

bool publish_discovery_announcement() {
  auto desired = announcement_key();
  const int64_t expires_at_ms = desired.expires_at_ms;
  const int64_t now_epoch_ms = static_cast<int64_t>(time(nullptr)) * 1000;
  if (expires_at_ms <= now_epoch_ms) {
    return false;
  }
  if (desired.address.empty()) return false;
  uint64_t generation;
  {
    std::lock_guard<std::mutex> lock(g_announcement_mutex);
    if (g_announcement_acknowledged && g_acknowledged == desired) return true;
    generation = g_announcement_generation;
  }
  const PlatformLinkConfig &runtime = runtime_config();
  char session_id[32] = {};
  std::snprintf(session_id, sizeof(session_id), "luna-%08lx",
                static_cast<unsigned long>(desired.nonce));

  JsonDocument doc;
  doc["discoveryEnabled"] = true;
  doc["discoverySessionId"] = session_id;
  doc["discoveryExpiresAtMs"] = expires_at_ms;
  doc["localAddress"] = desired.address;
  doc["controlBaseUrl"] = std::string("http://") + desired.address;
  doc["displayName"] = desired.name;
  doc["boardType"] =
      runtime.board_type.empty() ? "unknown" : runtime.board_type;
  doc["model"] =
      runtime.model.empty() ? "LunarEdu ESP32 Device" : runtime.model;
  doc["firmwareVersion"] = firmware_version();
  add_capabilities(doc["capabilities"].to<JsonArray>());
  std::string body;
  serializeJson(doc, body);

  int status_code = -1;
  if (!perform_request("POST", "/api/link/device-runtime/discovery",
                       &body, nullptr, &status_code)) {
    // Discovery is an address-refresh hint, not a claim channel. The server
    // may reject an expired/invalid announcement; stop a rejected window so
    // the device does not generate a retry storm.
    if (status_code == 409) {
      end_discovery();
    }
    return false;
  }
  bool acknowledged_current = false;
  {
    std::lock_guard<std::mutex> lock(g_announcement_mutex);
    // A reconnect/reset during I/O invalidates the old acknowledgement.
    if (generation == g_announcement_generation) {
      g_acknowledged = std::move(desired);
      g_announcement_acknowledged = true;
      acknowledged_current = true;
    }
  }
  if (acknowledged_current) record_product_profile_success();
  return true;
}

bool begin_discovery(uint32_t duration_ms) {
  std::string clock_error;
  if (!ensure_network_clock(clock_error)) {
    ESP_LOGW(kTag, "failed to synchronize clock before LAN discovery: %s",
         clock_error.c_str());
    return false;
  }
  g_discovery_nonce.store(system_random_u32());
  g_discovery_expires_at_ms.store(
      static_cast<int64_t>(time(nullptr)) * 1000 + duration_ms);
  invalidate_discovery_announcement();
  ESP_LOGI(kTag, "LAN discovery enabled for %lu ms",
       static_cast<unsigned long>(duration_ms));
  return true;
}

bool discovery_active() {
  return g_discovery_expires_at_ms.load() > static_cast<int64_t>(time(nullptr)) * 1000;
}

} // namespace lunaredu::lunaros::platform_link
